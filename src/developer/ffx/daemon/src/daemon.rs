// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::constants::{get_socket, CURRENT_EXE_BUILDID},
    anyhow::{anyhow, bail, Context, Result},
    ascendd::Ascendd,
    async_trait::async_trait,
    diagnostics_data::Timestamp,
    ffx_build_version::build_info,
    ffx_core::metrics::{add_daemon_launch_event, add_daemon_metrics_event},
    ffx_core::TryStreamUtilExt,
    ffx_daemon_core::events::{self, EventHandler},
    ffx_daemon_events::{
        DaemonEvent, TargetConnectionState, TargetEvent, TargetInfo, WireTrafficType,
    },
    ffx_daemon_services::create_service_register_map,
    ffx_daemon_target::logger::streamer::{DiagnosticsStreamer, GenericDiagnosticsStreamer},
    ffx_daemon_target::target::Target,
    ffx_daemon_target::target_collection::TargetCollection,
    ffx_daemon_target::zedboot::zedboot_discovery,
    fidl::endpoints::{ClientEnd, DiscoverableProtocolMarker, ProtocolMarker, RequestStream},
    fidl_fuchsia_developer_bridge::{
        self as bridge, DaemonError, DaemonMarker, DaemonRequest, DaemonRequestStream,
        DiagnosticsStreamError, LogSession, RepositoryRegistryMarker, SessionSpec, StreamMode,
        TargetCollectionMarker, TimeBound,
    },
    fidl_fuchsia_developer_remotecontrol::{
        ArchiveIteratorEntry, ArchiveIteratorError, ArchiveIteratorRequest, DiagnosticsData,
        InlineData, RemoteControlMarker, RemoteControlProxy,
    },
    fidl_fuchsia_overnet::Peer,
    fidl_fuchsia_overnet::{ServiceProviderRequest, ServiceProviderRequestStream},
    fidl_fuchsia_overnet_protocol::NodeId,
    fuchsia_async::{Task, TimeoutExt, Timer},
    futures::prelude::*,
    hoist::{hoist, OvernetInstance},
    rcs::RcsConnection,
    services::{DaemonServiceProvider, ServiceError, ServiceRegister},
    std::cell::Cell,
    std::collections::HashSet,
    std::hash::{Hash, Hasher},
    std::rc::Rc,
    std::sync::Arc,
    std::time::{Duration, Instant},
};

// Daemon

// This is just for mocking config values for unit testing.
#[async_trait(?Send)]
trait ConfigReader: Send + Sync {
    async fn get(&self, q: &str) -> Result<Option<String>>;
}

#[derive(Default)]
struct DefaultConfigReader {}

#[async_trait(?Send)]
impl ConfigReader for DefaultConfigReader {
    async fn get(&self, q: &str) -> Result<Option<String>> {
        Ok(ffx_config::get(q).await?)
    }
}

pub struct DaemonEventHandler {
    target_collection: Rc<TargetCollection>,
}

impl DaemonEventHandler {
    fn new(target_collection: Rc<TargetCollection>) -> Self {
        Self { target_collection }
    }

    async fn handle_overnet_peer(&self, node_id: u64) {
        let rcs = match RcsConnection::new(&mut NodeId { id: node_id }) {
            Ok(rcs) => rcs,
            Err(e) => {
                log::error!("Target from Overnet {} failed to connect to RCS: {:?}", node_id, e);
                return;
            }
        };

        let target = match Target::from_rcs_connection(rcs).await {
            Ok(target) => target,
            Err(err) => {
                log::error!("Target from Overnet {} could not be identified: {:?}", node_id, err);
                return;
            }
        };

        log::trace!("Target from Overnet {} is {}", node_id, target.nodename_str());
        let target = self.target_collection.merge_insert(target);
        target.run_logger();
    }

    async fn handle_overnet_peer_lost(&self, node_id: u64) {
        if let Some(target) = self
            .target_collection
            .targets()
            .iter()
            .find(|target| target.overnet_node_id() == Some(node_id))
        {
            target.disconnect();
        }
    }

    fn handle_fastboot(&self, t: TargetInfo) {
        log::trace!(
            "Found new target via fastboot: {}",
            t.nodename.clone().unwrap_or("<unknown>".to_string())
        );
        let target = self.target_collection.merge_insert(Target::from_target_info(t.into()));
        target.update_connection_state(|s| match s {
            TargetConnectionState::Disconnected | TargetConnectionState::Fastboot(_) => {
                TargetConnectionState::Fastboot(Instant::now())
            }
            _ => s,
        });
    }

    async fn handle_zedboot(&self, t: TargetInfo) {
        log::trace!(
            "Found new target via zedboot: {}",
            t.nodename.clone().unwrap_or("<unknown>".to_string())
        );
        let target = self.target_collection.merge_insert(Target::from_netsvc_target_info(t.into()));
        target.update_connection_state(|s| match s {
            TargetConnectionState::Disconnected | TargetConnectionState::Zedboot(_) => {
                TargetConnectionState::Zedboot(Instant::now())
            }
            _ => s,
        });
    }
}

#[async_trait(?Send)]
impl DaemonServiceProvider for Daemon {
    async fn open_service_proxy(&self, service_name: String) -> Result<fidl::Channel> {
        let (server, client) = fidl::Channel::create().context("creating zx channel")?;
        self.service_register
            .open(
                service_name,
                services::Context::new(self.clone()),
                fidl::AsyncChannel::from_channel(server)?,
            )
            .await?;
        Ok(client)
    }

    async fn open_target_proxy(
        &self,
        target_identifier: Option<String>,
        service_selector: fidl_fuchsia_diagnostics::Selector,
    ) -> Result<fidl::Channel> {
        let (_, channel) =
            self.open_target_proxy_with_info(target_identifier, service_selector).await?;
        Ok(channel)
    }

    async fn get_target_event_queue(
        &self,
        target_identifier: Option<String>,
    ) -> Result<(Rc<Target>, events::Queue<TargetEvent>)> {
        let target = self
            .get_target(target_identifier)
            .await
            .map_err(|e| anyhow!("{:#?}", e))
            .context("getting default target")?;
        target.run_host_pipe();
        let events = target.events.clone();
        Ok((target, events))
    }

    async fn open_target_proxy_with_info(
        &self,
        target_identifier: Option<String>,
        service_selector: fidl_fuchsia_diagnostics::Selector,
    ) -> Result<(bridge::Target, fidl::Channel)> {
        let target = self.get_rcs_ready_target(target_identifier).await?;
        let rcs = target
            .rcs()
            .ok_or(anyhow!("rcs disconnected after event fired"))
            .context("getting rcs instance")?;
        let (server, client) = fidl::Channel::create().context("creating zx channel")?;

        // TODO(awdavies): Handle these errors properly so the client knows what happened.
        rcs.proxy
            .connect(service_selector, server)
            .await
            .context("FIDL connection")?
            .map_err(|e| anyhow!("{:#?}", e))
            .context("proxy connect")?;
        Ok((target.as_ref().into(), client))
    }

    async fn open_remote_control(
        &self,
        target_identifier: Option<String>,
    ) -> Result<RemoteControlProxy> {
        let target = self.get_rcs_ready_target(target_identifier).await?;
        // Ensure auto-connect has at least started.
        let mut rcs = target
            .rcs()
            .ok_or(anyhow!("rcs disconnected after event fired"))
            .context("getting rcs instance")?;
        let (proxy, remote) = fidl::endpoints::create_proxy::<RemoteControlMarker>()?;
        rcs.copy_to_channel(remote.into_channel())?;
        Ok(proxy)
    }

    async fn daemon_event_queue(&self) -> events::Queue<DaemonEvent> {
        self.event_queue.clone()
    }

    async fn get_target_collection(&self) -> Result<Rc<TargetCollection>> {
        Ok(self.target_collection.clone())
    }
}

#[async_trait(?Send)]
impl EventHandler<DaemonEvent> for DaemonEventHandler {
    async fn on_event(&self, event: DaemonEvent) -> Result<events::Status> {
        log::info!("! DaemonEvent::{:?}", event);

        match event {
            DaemonEvent::WireTraffic(traffic) => match traffic {
                WireTrafficType::Mdns(t) => {
                    log::warn!("mdns traffic fired in daemon. This is deprecated: {:?}", t);
                }
                WireTrafficType::Fastboot(t) => {
                    self.handle_fastboot(t);
                }
                WireTrafficType::Zedboot(t) => {
                    self.handle_zedboot(t).await;
                }
            },
            DaemonEvent::OvernetPeer(node_id) => {
                self.handle_overnet_peer(node_id).await;
            }
            DaemonEvent::OvernetPeerLost(node_id) => {
                self.handle_overnet_peer_lost(node_id).await;
            }
            _ => (),
        }

        // This handler is never done unless the target_collection is dropped.
        Ok(events::Status::Waiting)
    }
}

#[derive(Clone)]
/// Defines the daemon object. This is used by "ffx daemon start".
///
/// Typical usage is:
///   let mut daemon = ffx_daemon::Daemon::new();
///   daemon.start().await
pub struct Daemon {
    // The event queue is a collection of subscriptions to which DaemonEvents will be published.
    event_queue: events::Queue<DaemonEvent>,
    // All the targets currently known to the daemon.
    // This may include targets the daemon has no access to.
    target_collection: Rc<TargetCollection>,
    // ascendd is the overnet daemon running on the Linux host. It manages the mesh and the
    // connections to the devices and other peers (for example, a connection to the frontend).
    // With ffx, ascendd is embedded within the ffx daemon (when ffx daemon is launched, we don’t
    // need an extra process for ascendd).
    ascendd: Rc<Cell<Option<Ascendd>>>,
    // Handles the registered FIDL services and associated handles. This is initialized with the
    // list of services defined in src/developer/ffx/daemon/services/BUILD.gn (the deps field in
    // ffx_service) using the macro generate_service_map in
    // src/developer/ffx/build/templates/services_macro.md.
    service_register: ServiceRegister,
    // All the persistent long running tasks spawned by the daemon. The tasks are standalone. That
    // means that they execute by themselves without any intervention from the daemon.
    // The purpose of this vector is to keep the reference strong count positive until the daemon is
    // dropped.
    tasks: Vec<Rc<Task<()>>>,
}

impl Daemon {
    pub fn new() -> Daemon {
        let target_collection = Rc::new(TargetCollection::new());
        let event_queue = events::Queue::new(&target_collection);
        target_collection.set_event_queue(event_queue.clone());

        Self {
            target_collection,
            event_queue,
            service_register: ServiceRegister::new(create_service_register_map()),
            ascendd: Rc::new(Cell::new(None)),
            tasks: Vec::new(),
        }
    }

    pub async fn start(&mut self) -> Result<()> {
        self.log_startup_info().await?;

        self.start_services().await?;
        self.start_discovery().await?;
        self.start_ascendd().await?;
        self.start_target_expiry(Duration::from_secs(1));
        self.serve().await
    }

    async fn log_startup_info(&self) -> Result<()> {
        let pid = std::process::id();
        let hash: String =
            ffx_config::get((CURRENT_EXE_BUILDID, ffx_config::ConfigLevel::Runtime)).await?;
        let version_info = build_info();
        let commit_hash = version_info.commit_hash.as_deref().unwrap_or("<unknown>");
        let commit_timestamp =
            version_info.commit_timestamp.map(|t| t.to_string()).unwrap_or("<unknown>".to_owned());
        let build_version = version_info.build_version.as_deref().unwrap_or("<unknown>");

        log::info!(
            "Beginning daemon startup\nBuild Version: {}\nCommit Timestamp: {}\nCommit Hash: {}\nBinary Hash: {}\nPID: {}",
            build_version,
            commit_timestamp,
            commit_hash,
            hash,
            pid
        );
        add_daemon_launch_event().await;
        Ok(())
    }

    async fn start_services(&mut self) -> Result<()> {
        let cx = services::Context::new(self.clone());
        let ((), ()) = futures::future::try_join(
            self.service_register
                .start(RepositoryRegistryMarker::PROTOCOL_NAME.to_string(), cx.clone()),
            self.service_register.start(TargetCollectionMarker::PROTOCOL_NAME.to_string(), cx),
        )
        .await?;
        Ok(())
    }

    /// Awaits a target that has RCS active.
    async fn get_rcs_ready_target(&self, target_query: Option<String>) -> Result<Rc<Target>> {
        let target = self
            .get_target(target_query)
            .await
            .map_err(|e| anyhow!("{:#?}", e))
            .context("getting default target")?;
        if matches!(target.get_connection_state(), TargetConnectionState::Fastboot(_)) {
            let nodename = target.nodename().unwrap_or("<No Nodename>".to_string());
            bail!("Attempting to open RCS on a fastboot target: {}", nodename);
        }
        if matches!(target.get_connection_state(), TargetConnectionState::Zedboot(_)) {
            let nodename = target.nodename().unwrap_or("<No Nodename>".to_string());
            bail!("Attempting to connect to RCS on a zedboot target: {}", nodename);
        }
        // Ensure auto-connect has at least started.
        target.run_host_pipe();
        target
            .events
            .wait_for(None, |e| e == TargetEvent::RcsActivated)
            .await
            .context("waiting for RCS activation")?;
        Ok(target)
    }

    /// Start all discovery tasks
    async fn start_discovery(&mut self) -> Result<()> {
        let daemon_event_handler = DaemonEventHandler::new(self.target_collection.clone());
        self.event_queue.add_handler(daemon_event_handler).await;

        // TODO: these tasks could and probably should be managed by the daemon
        // instead of being detached.
        Daemon::spawn_onet_discovery(self.event_queue.clone());
        self.tasks.push(Rc::new(zedboot_discovery(self.event_queue.clone())?));
        Ok(())
    }

    async fn start_ascendd(&mut self) -> Result<()> {
        // Start the ascendd socket only after we have registered our services.
        log::info!("Starting ascendd");

        let ascendd = Ascendd::new(
            ascendd::Opt { sockpath: Some(get_socket().await), ..Default::default() },
            // TODO: this just prints serial output to stdout - ffx probably wants to take a more
            // nuanced approach here.
            blocking::Unblock::new(std::io::stdout()),
        )
        .await?;

        self.ascendd.replace(Some(ascendd));

        Ok(())
    }

    fn start_target_expiry(&mut self, frequency: Duration) {
        let target_collection = Rc::downgrade(&self.target_collection);
        self.tasks.push(Rc::new(Task::local(async move {
            loop {
                Timer::new(frequency.clone()).await;

                match target_collection.upgrade() {
                    Some(target_collection) => {
                        for target in target_collection.targets() {
                            // Manually-added remote targets will not be discovered by mDNS,
                            // and as a result will not have host-pipe triggered automatically
                            // by the mDNS event handler.
                            if target.is_manual() {
                                target.run_host_pipe();
                            }
                            target.expire_state();
                        }
                    }
                    None => return,
                }
            }
        })))
    }

    /// get_target attempts to get the target that matches the match string if
    /// provided, otherwise the default target from the target collection.
    async fn get_target(&self, matcher: Option<String>) -> Result<Rc<Target>, DaemonError> {
        #[cfg(not(test))]
        const GET_TARGET_TIMEOUT: Duration = Duration::from_secs(8);
        #[cfg(test)]
        const GET_TARGET_TIMEOUT: Duration = Duration::from_secs(1);

        // TODO(72818): make target match timeout configurable / paramterable
        self.target_collection
            .wait_for_match(matcher)
            .on_timeout(GET_TARGET_TIMEOUT, || match self.target_collection.is_empty() {
                true => Err(DaemonError::TargetCacheEmpty),
                false => Err(DaemonError::TargetNotFound),
            })
            .await
    }

    async fn handle_requests_from_stream(&self, stream: DaemonRequestStream) -> Result<()> {
        stream
            .map_err(|e| anyhow!("reading FIDL stream: {:#}", e))
            .try_for_each_concurrent_while_connected(None, |r| self.handle_request(r))
            .await
    }

    fn spawn_onet_discovery(queue: events::Queue<DaemonEvent>) {
        fuchsia_async::Task::local(async move {
            let mut known_peers: HashSet<PeerSetElement> = Default::default();

            loop {
                let svc = match hoist().connect_as_service_consumer() {
                    Ok(svc) => svc,
                    Err(err) => {
                        log::info!("Overnet setup failed: {}, will retry in 1s", err);
                        Timer::new(Duration::from_secs(1)).await;
                        continue;
                    }
                };
                loop {
                    match svc.list_peers().await {
                        Ok(new_peers) => {
                            known_peers =
                                Self::handle_overnet_peers(&queue, known_peers, new_peers);
                        }
                        Err(err) => {
                            log::info!("Overnet peer discovery failed: {}, will retry", err);
                            Timer::new(Duration::from_secs(1)).await;
                            // break out of the peer discovery loop on error in
                            // order to reconnect, in case the error causes the
                            // overnet interface to go bad.
                            break;
                        }
                    };
                }
            }
        })
        .detach();
    }

    fn handle_overnet_peers(
        queue: &events::Queue<DaemonEvent>,
        known_peers: HashSet<PeerSetElement>,
        peers: Vec<Peer>,
    ) -> HashSet<PeerSetElement> {
        let mut new_peers: HashSet<PeerSetElement> = Default::default();
        for peer in peers {
            new_peers.insert(PeerSetElement(peer));
        }

        for peer in new_peers.difference(&known_peers) {
            let peer = &peer.0;
            let peer_has_rcs = peer
                .description
                .services
                .as_ref()
                .map(|v| v.contains(&RemoteControlMarker::NAME.to_string()))
                .unwrap_or(false);
            if peer_has_rcs {
                queue.push(DaemonEvent::OvernetPeer(peer.id.id)).unwrap_or_else(|err| {
                    log::warn!(
                        "Overnet discovery failed to enqueue event {:?}: {}",
                        DaemonEvent::OvernetPeer(peer.id.id),
                        err
                    );
                });
            }
        }

        for peer in known_peers.difference(&new_peers) {
            let peer = &peer.0;
            queue.push(DaemonEvent::OvernetPeerLost(peer.id.id)).unwrap_or_else(|err| {
                log::warn!(
                    "Overnet discovery failed to enqueue event {:?}: {}",
                    DaemonEvent::OvernetPeerLost(peer.id.id),
                    err
                );
            });
        }

        new_peers
    }

    async fn handle_request(&self, req: DaemonRequest) -> Result<()> {
        log::debug!("daemon received request: {:?}", req);

        match req {
            DaemonRequest::Crash { .. } => {
                add_daemon_metrics_event("panic").await;
                panic!("instructed to crash by client!");
            }
            DaemonRequest::EchoString { value, responder } => {
                log::info!("Received echo request for string {:?}", value);
                responder.send(value.as_ref()).context("error sending response")?;
                log::info!("echo response sent successfully");
                add_daemon_metrics_event("echo").await;
            }
            // Hang intends to block the reactor indefinitely, however
            // that's a little tricky to do exactly. This approximation
            // is strong enough for right now, though it may be awoken
            // again periodically on timers, depending on implementation
            // details of the underlying reactor.
            DaemonRequest::Hang { .. } => loop {
                add_daemon_metrics_event("hang").await;
                std::thread::park()
            },
            DaemonRequest::Quit { responder } => {
                log::info!("Received quit request.");

                match std::fs::remove_file(get_socket().await) {
                    Ok(()) => {}
                    Err(e) => log::error!("failed to remove socket file: {}", e),
                }

                if cfg!(test) {
                    panic!("quit() should not be invoked in test code");
                }

                self.service_register
                    .shutdown(services::Context::new(self.clone()))
                    .await
                    .unwrap_or_else(|e| log::error!("shutting down service register: {:?}", e));

                add_daemon_metrics_event("quit").await;
                // It is desirable for the client to receive an ACK for the quit
                // request. As Overnet has a potentially complicated routing
                // path, it is tricky to implement some notion of a bounded
                // "flush" for this response, however in practice it is only
                // necessary here to wait long enough for the message to likely
                // leave the local process before exiting. Enqueue a detached
                // timer to shut down the daemon before sending the response.
                // This is detached because once the client receives the
                // response, the client will disconnect it's socket. If the
                // local reactor observes this disconnection before the timer
                // expires, an in-line timer wait would never fire, and the
                // daemon would never exit.
                Task::local(
                    Timer::new(std::time::Duration::from_millis(20)).map(|_| std::process::exit(0)),
                )
                .detach();

                responder.send(true).context("error sending response")?;
            }
            DaemonRequest::GetVersionInfo { responder } => {
                return responder.send(build_info()).context("sending GetVersionInfo response");
            }
            DaemonRequest::GetHash { responder } => {
                let hash: String =
                    ffx_config::get((CURRENT_EXE_BUILDID, ffx_config::ConfigLevel::Runtime))
                        .await?;
                responder.send(&hash).context("error sending response")?;
            }
            DaemonRequest::ConnectToService { name, server_channel, responder } => {
                let name_for_analytics = name.clone();
                match self
                    .service_register
                    .open(
                        name,
                        services::Context::new(self.clone()),
                        fidl::AsyncChannel::from_channel(server_channel)?,
                    )
                    .await
                {
                    Ok(()) => responder.send(&mut Ok(())).context("fidl response")?,
                    Err(e) => {
                        log::error!("{}", e);
                        match e {
                            ServiceError::NoServiceFound(_) => {
                                responder.send(&mut Err(DaemonError::ServiceNotFound))?
                            }
                            ServiceError::StreamOpenError(_) => {
                                responder.send(&mut Err(DaemonError::ServiceOpenError))?
                            }
                            ServiceError::BadRegisterState(_)
                            | ServiceError::DuplicateTaskId(..) => {
                                responder.send(&mut Err(DaemonError::BadServiceRegisterState))?
                            }
                        }
                    }
                }
                add_daemon_metrics_event(
                    format!("connect_to_service: {}", &name_for_analytics).as_str(),
                )
                .await;
            }
            DaemonRequest::StreamDiagnostics {
                target: target_str,
                parameters,
                iterator,
                responder,
            } => {
                add_daemon_metrics_event("stream_diagnostics").await;
                let stream_mode = if let Some(mode) = parameters.stream_mode {
                    mode
                } else {
                    log::info!("StreamDiagnostics failed: stream mode is required");
                    return responder
                        .send(&mut Err(DiagnosticsStreamError::MissingParameter))
                        .context("sending missing parameter response");
                };

                let target_result = self
                    .get_target(target_str.clone())
                    .on_timeout(Duration::from_secs(1), || Err(DaemonError::Timeout))
                    .await;
                let (target_identifier, stream) = if stream_mode == StreamMode::SnapshotAll {
                    let target_str = if let Some(target_str) = target_str {
                        target_str
                    } else {
                        log::warn!(
                            "StreamDiagnostics failed: Missing target string in SnapshotAll mode."
                        );
                        return responder
                            .send(&mut Err(DiagnosticsStreamError::MissingParameter))
                            .context("sending missing parameter response");
                    };

                    let session = if let Some(session) = parameters.session {
                        session
                    } else {
                        log::warn!(
                            "StreamDiagnostics failed: Missing session in SnapshotAll mode."
                        );
                        return responder
                            .send(&mut Err(DiagnosticsStreamError::MissingParameter))
                            .context("sending missing parameter response");
                    };

                    let mut streams =
                        DiagnosticsStreamer::list_sessions(Some(target_str.clone())).await?;
                    if streams.is_empty() {
                        responder
                            .send(&mut Err(DiagnosticsStreamError::NoMatchingOfflineTargets))?;
                        return Ok(());
                    }

                    let streams = streams
                        .remove(&target_str)
                        .context("getting stream by target name. should be infallible")?;

                    if streams.is_empty() {
                        responder
                            .send(&mut Err(DiagnosticsStreamError::NoMatchingOfflineTargets))?;
                        return Ok(());
                    }

                    match session {
                        SessionSpec::TimestampNanos(ts) => {
                            let mut result_stream = None;

                            for stream in streams.into_iter() {
                                let session_ts = stream.session_timestamp_nanos().await;
                                if Some(ts) == session_ts.map(|t| t as u64) {
                                    result_stream = Some(Arc::new(stream));
                                    break;
                                }
                            }

                            if let Some(stream) = result_stream {
                                (target_str, stream)
                            } else {
                                responder.send(&mut Err(
                                    DiagnosticsStreamError::NoMatchingOfflineSessions,
                                ))?;
                                return Ok(());
                            }
                        }
                        SessionSpec::Relative(rel) => {
                            let mut sorted = vec![];
                            for stream in streams.into_iter() {
                                let ts = stream.session_timestamp_nanos().await;
                                if let Some(ts) = ts {
                                    sorted.push((ts, stream));
                                }
                            }

                            sorted.sort_by_key(|t| -t.0);

                            // If there is not an active session, we offset the session number relative to the
                            // index by 1, as `0` is reserved for an active session
                            let has_active_session = if let Ok(ref t) = target_result {
                                t.is_logger_running()
                            } else {
                                false
                            };

                            let rel = if has_active_session {
                                rel
                            } else {
                                if rel == 0 {
                                    return responder
                                        .send(&mut Err(
                                            DiagnosticsStreamError::NoMatchingOfflineSessions,
                                        ))
                                        .context("sending no matching offline sessions response");
                                }
                                rel - 1
                            };

                            if let Some((_, stream)) = sorted.into_iter().nth(rel as usize) {
                                (target_str, Arc::new(stream))
                            } else {
                                return responder
                                    .send(&mut Err(
                                        DiagnosticsStreamError::NoMatchingOfflineSessions,
                                    ))
                                    .context("sending no offline sessions response");
                            }
                        }
                        _ => bail!("unexpected SessionSpec value"),
                    }
                } else {
                    match target_result {
                        Ok(t) => (t.nodename_str(), t.stream_info()),
                        Err(DaemonError::Timeout) => {
                            return responder
                                .send(&mut Err(DiagnosticsStreamError::NoMatchingTargets))
                                .context("sending no matching targets response");
                        }
                        Err(e) => {
                            log::warn!(
                                "got error fetching target with filter '{}': {:#?}",
                                target_str.as_ref().unwrap_or(&String::default()),
                                e
                            );
                            return responder
                                .send(&mut Err(DiagnosticsStreamError::TargetMatchFailed))
                                .context("sending TargetMatchFailed response");
                        }
                    }
                };

                match stream
                    .wait_for_setup()
                    .map(|_| Ok(()))
                    .on_timeout(Duration::from_secs(3), || {
                        Err(DiagnosticsStreamError::NoStreamForTarget)
                    })
                    .await
                {
                    Ok(_) => {}
                    Err(e) => {
                        // TODO(jwing): we should be able to interact with inactive targets here for
                        // stream modes that don't involve subscription.
                        return responder.send(&mut Err(e)).context("sending error response");
                    }
                }

                let min_timestamp = match parameters.min_timestamp_nanos {
                    Some(TimeBound::Absolute(t)) => {
                        if let Some(session) = stream.session_timestamp_nanos().await {
                            Some(Timestamp::from(t as i64 - session))
                        } else {
                            None
                        }
                    }
                    Some(TimeBound::Monotonic(t)) => Some(Timestamp::from(t as i64)),
                    Some(bound) => {
                        log::error!("Got unexpected TimeBound field {:?}", bound);
                        responder.send(&mut Err(DiagnosticsStreamError::GenericError))?;
                        return Ok(());
                    }
                    None => {
                        parameters.min_target_timestamp_nanos.map(|t| Timestamp::from(t as i64))
                    }
                };

                let mut log_iterator =
                    stream.stream_entries(parameters.stream_mode.unwrap(), min_timestamp).await?;
                let task = Task::local(async move {
                    let mut iter_stream = iterator.into_stream()?;

                    while let Some(request) = iter_stream.next().await {
                        match request? {
                            ArchiveIteratorRequest::GetNext { responder } => {
                                let res = log_iterator.iter().await?;
                                match res {
                                    Some(Ok(entry)) => {
                                        // If the entry is small enough to fit into a FIDL message
                                        // we send it using the Inline variant. Otherwise, we use
                                        // the Socket variant by sending one end of the socket as a
                                        // response and sending the data into the other end of the
                                        // socket.
                                        // TODO(fxbug.dev/81310): This should be unified across the
                                        // daemon and bridge.
                                        let data = serde_json::to_string(&entry)?;
                                        if data.len()
                                            <= fidl_fuchsia_logger::MAX_DATAGRAM_LEN_BYTES as usize
                                        {
                                            responder.send(&mut Ok(vec![
                                                ArchiveIteratorEntry {
                                                    diagnostics_data: Some(
                                                        DiagnosticsData::Inline(InlineData {
                                                            data,
                                                            truncated_chars: 0,
                                                        }),
                                                    ),
                                                    ..ArchiveIteratorEntry::EMPTY
                                                },
                                            ]))?;
                                        } else {
                                            let (socket, tx_socket) =
                                                fuchsia_async::emulated_handle::Socket::create(
                                                    fuchsia_async::emulated_handle::SocketOpts::STREAM,
                                                )?;
                                            let mut tx_socket =
                                                fuchsia_async::Socket::from_socket(tx_socket)?;
                                            // We send one end of the socket back to the caller.
                                            // The receiver will need to read the socket content to
                                            // get the data.
                                            let response = vec![ArchiveIteratorEntry {
                                                diagnostics_data: Some(DiagnosticsData::Socket(
                                                    socket,
                                                )),
                                                ..ArchiveIteratorEntry::EMPTY
                                            }];
                                            responder.send(&mut Ok(response))?;
                                            // We write all the data to the other end of the
                                            // socket.
                                            tx_socket.write_all(data.as_bytes()).await?;
                                        }
                                    }
                                    Some(Err(e)) => {
                                        log::warn!("got error streaming diagnostics: {}", e);
                                        responder
                                            .send(&mut Err(ArchiveIteratorError::DataReadFailed))?;
                                    }
                                    None => {
                                        responder.send(&mut Ok(vec![]))?;
                                        break;
                                    }
                                }
                            }
                        }
                    }

                    Ok::<(), anyhow::Error>(())
                });
                responder.send(&mut Ok(LogSession {
                    target_identifier: Some(target_identifier),
                    session_timestamp_nanos: stream
                        .session_timestamp_nanos()
                        .await
                        .map(|t| t as u64),
                    ..LogSession::EMPTY
                }))?;
                task.await?;
            }
        }

        Ok(())
    }

    async fn serve(&self) -> Result<()> {
        let (s, p) = fidl::Channel::create().context("failed to create zx channel")?;
        let chan = fidl::AsyncChannel::from_channel(s).context("failed to make async channel")?;
        let mut stream = ServiceProviderRequestStream::from_channel(chan);

        log::info!("Starting daemon overnet server");
        hoist::hoist().publish_service(DaemonMarker::NAME, ClientEnd::new(p))?;

        log::info!("Starting daemon serve loop");
        while let Some(ServiceProviderRequest::ConnectToService {
            chan,
            info: _,
            control_handle: _control_handle,
        }) = stream.try_next().await.context("error running service provider server")?
        {
            log::trace!("Received service request for service");
            let chan =
                fidl::AsyncChannel::from_channel(chan).context("failed to make async channel")?;
            let daemon_clone = self.clone();
            Task::local(async move {
                daemon_clone
                    .handle_requests_from_stream(DaemonRequestStream::from_channel(chan))
                    .await
                    .unwrap_or_else(|err| panic!("fatal error handling request: {:?}", err));
            })
            .detach();
        }
        Ok(())
    }
}

// PeerSetElement wraps an overnet Peer object for inclusion in a Set
// or other collection reliant on Eq and HAsh, using the NodeId as the
// discriminator.
struct PeerSetElement(Peer);
impl PartialEq for PeerSetElement {
    fn eq(&self, other: &Self) -> bool {
        self.0.id == other.0.id
    }
}
impl Eq for PeerSetElement {}
impl Hash for PeerSetElement {
    fn hash<H: Hasher>(&self, state: &mut H) {
        self.0.id.hash(state);
    }
}

#[cfg(test)]
mod test {
    use {
        super::*,
        addr::TargetAddr,
        fidl_fuchsia_developer_bridge::{DaemonMarker, DaemonProxy},
        fidl_fuchsia_developer_remotecontrol::RemoteControlMarker,
        fidl_fuchsia_overnet_protocol::PeerDescription,
        fuchsia_async::Task,
        matches::assert_matches,
        std::cell::RefCell,
        std::collections::BTreeSet,
        std::iter::FromIterator,
    };

    fn spawn_test_daemon() -> (DaemonProxy, Daemon, Task<Result<()>>) {
        let d = Daemon::new();

        let (proxy, stream) = fidl::endpoints::create_proxy_and_stream::<DaemonMarker>().unwrap();

        let d2 = d.clone();
        let task = Task::local(async move { d2.handle_requests_from_stream(stream).await });

        (proxy, d, task)
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_open_rcs_on_fastboot_error() {
        let (_proxy, daemon, _task) = spawn_test_daemon();
        let target = Target::new_with_serial("abc");
        daemon.target_collection.merge_insert(target);
        let result = daemon.open_remote_control(None).await;
        assert!(result.is_err());
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_open_rcs_on_zedboot_error() {
        let (_proxy, daemon, _task) = spawn_test_daemon();
        let target = Target::new_with_netsvc_addrs(
            Some("abc"),
            BTreeSet::from_iter(vec![TargetAddr::new("[fe80::1%1]:22").unwrap()].into_iter()),
        );
        daemon.target_collection.merge_insert(target);
        let result = daemon.open_remote_control(None).await;
        assert!(result.is_err());
    }

    struct FakeConfigReader {
        query_expected: String,
        value: String,
    }

    #[async_trait(?Send)]
    impl ConfigReader for FakeConfigReader {
        async fn get(&self, q: &str) -> Result<Option<String>> {
            assert_eq!(q, self.query_expected);
            Ok(Some(self.value.clone()))
        }
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_get_target_empty() {
        let d = Daemon::new();
        let nodename = "where-is-my-hasenpfeffer";
        let t = Target::new_autoconnected(nodename);
        d.target_collection.merge_insert(t.clone());
        assert_eq!(nodename, d.get_target(None).await.unwrap().nodename().unwrap());
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_get_target_query() {
        let d = Daemon::new();
        let nodename = "where-is-my-hasenpfeffer";
        let t = Target::new_autoconnected(nodename);
        d.target_collection.merge_insert(t.clone());
        assert_eq!(
            nodename,
            d.get_target(Some(nodename.to_string())).await.unwrap().nodename().unwrap()
        );
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_get_target_collection_empty_error() {
        let d = Daemon::new();
        assert_eq!(DaemonError::TargetCacheEmpty, d.get_target(None).await.unwrap_err());
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_get_target_ambiguous() {
        let d = Daemon::new();
        let t = Target::new_autoconnected("where-is-my-hasenpfeffer");
        let t2 = Target::new_autoconnected("it-is-rabbit-season");
        d.target_collection.merge_insert(t.clone());
        d.target_collection.merge_insert(t2.clone());
        assert_eq!(DaemonError::TargetAmbiguous, d.get_target(None).await.unwrap_err());
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_target_expiry() {
        let mut daemon = Daemon::new();
        let target = Target::new_named("goodbye-world");
        let then = Instant::now() - Duration::from_secs(10);
        target.update_connection_state(|_| TargetConnectionState::Mdns(then));
        daemon.target_collection.merge_insert(target.clone());

        assert_eq!(TargetConnectionState::Mdns(then), target.get_connection_state());

        daemon.start_target_expiry(Duration::from_millis(1));

        while target.get_connection_state() == TargetConnectionState::Mdns(then) {
            futures_lite::future::yield_now().await
        }

        assert_eq!(TargetConnectionState::Disconnected, target.get_connection_state());
    }

    struct NullDaemonEventSynthesizer();

    #[async_trait(?Send)]
    impl events::EventSynthesizer<DaemonEvent> for NullDaemonEventSynthesizer {
        async fn synthesize_events(&self) -> Vec<DaemonEvent> {
            return Default::default();
        }
    }

    #[test]
    fn test_handle_overnet_peers_known_peer_exclusion() {
        let queue = events::Queue::<DaemonEvent>::new(&Rc::new(NullDaemonEventSynthesizer {}));
        let mut known_peers: HashSet<PeerSetElement> = Default::default();

        let peer1 = Peer {
            description: PeerDescription {
                services: None,
                unknown_data: None,
                ..PeerDescription::EMPTY
            },
            id: NodeId { id: 1 },
            is_self: false,
        };
        let peer2 = Peer {
            description: PeerDescription {
                services: None,
                unknown_data: None,
                ..PeerDescription::EMPTY
            },
            id: NodeId { id: 2 },
            is_self: false,
        };

        let new_peers =
            Daemon::handle_overnet_peers(&queue, known_peers, vec![peer1.clone(), peer2.clone()]);
        assert!(new_peers.contains(&PeerSetElement(peer1.clone())));
        assert!(new_peers.contains(&PeerSetElement(peer2.clone())));

        known_peers = new_peers;

        let new_peers = Daemon::handle_overnet_peers(&queue, known_peers, vec![]);
        assert!(!new_peers.contains(&PeerSetElement(peer1.clone())));
        assert!(!new_peers.contains(&PeerSetElement(peer2.clone())));
    }

    struct DaemonEventRecorder {
        /// All events observed by the handler will be logged into this field.
        event_log: Rc<RefCell<Vec<DaemonEvent>>>,
    }
    #[async_trait(?Send)]
    impl EventHandler<DaemonEvent> for DaemonEventRecorder {
        async fn on_event(&self, event: DaemonEvent) -> Result<events::Status> {
            self.event_log.borrow_mut().push(event);
            Ok(events::Status::Waiting)
        }
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_handle_overnet_peer_leave_and_return() {
        let queue = events::Queue::<DaemonEvent>::new(&Rc::new(NullDaemonEventSynthesizer {}));
        let mut known_peers: HashSet<PeerSetElement> = Default::default();

        let peer1 = Peer {
            description: PeerDescription {
                services: Some(vec![RemoteControlMarker::NAME.to_string()]),
                unknown_data: None,
                ..PeerDescription::EMPTY
            },
            id: NodeId { id: 1 },
            is_self: false,
        };
        let peer2 = Peer {
            description: PeerDescription {
                services: Some(vec![RemoteControlMarker::NAME.to_string()]),
                unknown_data: None,
                ..PeerDescription::EMPTY
            },
            id: NodeId { id: 2 },
            is_self: false,
        };

        // First the targets are discovered:
        let new_peers =
            Daemon::handle_overnet_peers(&queue, known_peers, vec![peer1.clone(), peer2.clone()]);
        assert!(new_peers.contains(&PeerSetElement(peer1.clone())));
        assert!(new_peers.contains(&PeerSetElement(peer2.clone())));

        known_peers = new_peers;

        // Make a new queue so we don't get any of the historical events.
        let queue = events::Queue::<DaemonEvent>::new(&Rc::new(NullDaemonEventSynthesizer {}));
        let event_log = Rc::new(RefCell::new(Vec::<DaemonEvent>::new()));

        // Now wire up the event handler, we want to assert that we observe OvernetPeerLost events for the leaving targets.
        queue.add_handler(DaemonEventRecorder { event_log: event_log.clone() }).await;

        // Next the targets are lost:
        let new_peers = Daemon::handle_overnet_peers(&queue, known_peers, vec![]);
        assert!(!new_peers.contains(&PeerSetElement(peer1.clone())));
        assert!(!new_peers.contains(&PeerSetElement(peer2.clone())));

        let start = Instant::now();
        while event_log.borrow().len() != 2 {
            if Instant::now().duration_since(start) > Duration::from_secs(1) {
                break;
            }
            futures_lite::future::yield_now().await;
        }

        assert_eq!(event_log.borrow().len(), 2);
        assert_matches!(event_log.borrow()[0], DaemonEvent::OvernetPeerLost(_));
        assert_matches!(event_log.borrow()[1], DaemonEvent::OvernetPeerLost(_));

        known_peers = new_peers;

        assert_eq!(known_peers.len(), 0);

        // Make a new queue so we don't get any of the historical events.
        let queue = events::Queue::<DaemonEvent>::new(&Rc::new(NullDaemonEventSynthesizer {}));
        let event_log = Rc::new(RefCell::new(Vec::<DaemonEvent>::new()));

        // Now wire up the event handler, we want to assert that we observe NewTarget events for the returning targets.
        queue.add_handler(DaemonEventRecorder { event_log: event_log.clone() }).await;

        // Now the targets return:
        let new_peers =
            Daemon::handle_overnet_peers(&queue, known_peers, vec![peer1.clone(), peer2.clone()]);
        assert!(new_peers.contains(&PeerSetElement(peer1.clone())));
        assert!(new_peers.contains(&PeerSetElement(peer2.clone())));

        let start = Instant::now();
        while event_log.borrow().len() != 2 {
            if Instant::now().duration_since(start) > Duration::from_secs(1) {
                break;
            }
            futures_lite::future::yield_now().await;
        }

        // Ensure that we observed a new target event for each target that returned.
        assert_eq!(event_log.borrow().len(), 2);
        assert_matches!(event_log.borrow()[0], DaemonEvent::OvernetPeer(_));
        assert_matches!(event_log.borrow()[1], DaemonEvent::OvernetPeer(_));
    }
}
