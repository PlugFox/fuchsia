// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::Error,
    fidl_fuchsia_bluetooth_bredr::{ProfileMarker, ProfileProxy, ProfileRequestStream},
    fidl_fuchsia_bluetooth_rfcomm_test::{RfcommTestMarker, RfcommTestProxy},
    fuchsia_async as fasync,
    fuchsia_component::server::ServiceFs,
    fuchsia_component_test::{
        builder::{ComponentSource, RealmBuilder, RouteEndpoint},
        mock::{Mock, MockHandles},
    },
    futures::{channel::mpsc, SinkExt, StreamExt},
    realmbuilder_mock_helpers::add_fidl_service_handler,
    tracing::info,
};

/// RFCOMM component URL.
const RFCOMM_URL: &str = "fuchsia-pkg://fuchsia.com/bt-rfcomm-smoke-test#meta/bt-rfcomm.cm";

/// The different events generated by this test.
/// Note: In order to prevent the component under test from terminating, any FIDL channel or
/// request is kept alive.
enum Event {
    /// A fake RFCOMM client connecting to the Profile service.
    Client(Option<ProfileProxy>),
    /// A BR/EDR Profile service connection request was received - request was
    /// made by the RFCOMM component.
    Profile(Option<ProfileRequestStream>),
    /// A fake RFCOMM client connecting to the RfcommTest service.
    Test(Option<RfcommTestProxy>),
}

impl From<ProfileRequestStream> for Event {
    fn from(src: ProfileRequestStream) -> Self {
        Self::Profile(Some(src))
    }
}

/// Represents a fake RFCOMM client that requests the Profile service.
async fn mock_rfcomm_client(
    mut sender: mpsc::Sender<Event>,
    handles: MockHandles,
) -> Result<(), Error> {
    let profile_svc = handles.connect_to_service::<ProfileMarker>()?;
    sender.send(Event::Client(Some(profile_svc))).await.expect("failed sending ack to test");

    let test_svc = handles.connect_to_service::<RfcommTestMarker>()?;
    sender.send(Event::Test(Some(test_svc))).await.expect("failed sending ack to test");
    Ok(())
}

/// Simulates a component that provides the `bredr.Profile` service.
async fn mock_profile_component(
    sender: mpsc::Sender<Event>,
    handles: MockHandles,
) -> Result<(), Error> {
    let mut fs = ServiceFs::new();
    add_fidl_service_handler::<ProfileMarker, _>(&mut fs, sender.clone());
    let _ = fs.serve_connection(handles.outgoing_dir.into_channel())?;
    fs.collect::<()>().await;
    Ok(())
}

/// Tests that the v2 RFCOMM component has the correct topology and verifies that
/// it connects and provides the expected services.
#[fasync::run_singlethreaded(test)]
async fn rfcomm_v2_component_topology() {
    fuchsia_syslog::init().unwrap();
    info!("Starting RFCOMM v2 smoke test...");

    let (sender, mut receiver) = mpsc::channel(3);
    let profile_tx = sender.clone();
    let fake_client_tx = sender.clone();

    let mut builder = RealmBuilder::new().await.expect("Failed to create test realm builder");
    // The v2 component under test.
    let _ = builder
        .add_component("rfcomm", ComponentSource::url(RFCOMM_URL.to_string()))
        .await
        .expect("Failed adding rfcomm to topology");
    // Mock Profile component to receive bredr.Profile requests.
    let _ = builder
        .add_component(
            "fake-profile",
            ComponentSource::Mock(Mock::new({
                move |mock_handles: MockHandles| {
                    let sender = profile_tx.clone();
                    Box::pin(mock_profile_component(sender, mock_handles))
                }
            })),
        )
        .await
        .expect("Failed adding profile mock to topology");
    // Mock RFCOMM client that will connect to the Profile service and make a request.
    let _ = builder
        .add_eager_component(
            "fake-rfcomm-client",
            ComponentSource::Mock(Mock::new({
                move |mock_handles: MockHandles| {
                    let sender = fake_client_tx.clone();
                    Box::pin(mock_rfcomm_client(sender, mock_handles))
                }
            })),
        )
        .await
        .expect("Failed adding rfcomm client mock to topology");

    // Set up capabilities.
    let _ = builder
        .add_protocol_route::<ProfileMarker>(
            RouteEndpoint::component("fake-profile"),
            vec![RouteEndpoint::component("rfcomm")],
        )
        .expect("Failed adding route for profile service")
        .add_protocol_route::<ProfileMarker>(
            RouteEndpoint::component("rfcomm"),
            vec![RouteEndpoint::component("fake-rfcomm-client")],
        )
        .expect("Failed adding route for RFCOMM profile service")
        .add_protocol_route::<RfcommTestMarker>(
            RouteEndpoint::component("rfcomm"),
            vec![RouteEndpoint::component("fake-rfcomm-client")],
        )
        .expect("Failed adding route for RFCOMM profile service")
        .add_protocol_route::<fidl_fuchsia_logger::LogSinkMarker>(
            RouteEndpoint::AboveRoot,
            vec![
                RouteEndpoint::component("rfcomm"),
                RouteEndpoint::component("fake-profile"),
                RouteEndpoint::component("fake-rfcomm-client"),
            ],
        )
        .expect("Failed adding LogSink route to test components");
    let test_topology = builder.build().create().await.unwrap();

    let _ = test_topology.root.connect_to_binder().unwrap();

    // If the routing is correctly configured, we expect two events (in arbitrary order):
    //   1. `bt-rfcomm` connecting to the Profile service provided by `fake-profile`.
    //   2. `fake-rfcomm-client` connecting to the Profile service provided by `bt-rfcomm`.
    //   3. `fake-rfcomm-client` connecting to the RfcommTest service provided by `bt-rfcomm`.
    let mut events = Vec::new();
    let expected_number_of_events = 3;
    for i in 0..expected_number_of_events {
        let msg = format!("Unexpected error waiting for {:?} event", i);
        events.push(receiver.next().await.expect(&msg));
    }
    assert_eq!(events.len(), expected_number_of_events);
    let _mock_client_event = events
        .iter()
        .find(|&p| std::mem::discriminant(p) == std::mem::discriminant(&Event::Client(None)))
        .expect("Should receive client event");
    let _rfcomm_component_event = events
        .iter()
        .find(|&p| std::mem::discriminant(p) == std::mem::discriminant(&Event::Profile(None)))
        .expect("Should receive component event");
    let _rfcomm_test_event = events
        .iter()
        .find(|&p| std::mem::discriminant(p) == std::mem::discriminant(&Event::Test(None)))
        .expect("Should receive component event");

    info!("Finished RFCOMM smoke test");
}
