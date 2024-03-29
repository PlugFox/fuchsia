// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::Error,
    fuchsia_component::server as fserver,
    fuchsia_component_test::mock::MockHandles,
    futures::{Future, StreamExt},
};

/// Serves multiple FIDL clients, one at a time.
///
/// By serving clients one-at-a-time, we maximize the determinism
/// of the tests, and thereby (hopefully) minimize flakiness.
///
/// Note that `serve_clients()` calls `clone()` on the mock. Consequently,
/// `struct`s calling this function should ensure that their `clone()`
/// provides sensible semantics. In particular, if state mutated by one
/// client should be visible to other clients, the `struct` should make
/// that state shared across clones.
///
/// Future directions:
/// * We might change the function to take `Arc<ServiceParamType>`, to
///   ensure that the clones don't have independent copies of mutable
///   state (if we find a need for mocks with mutable state).
/// * We might add a `serve_clients_concurrently()` method, if a
///   need arises.
async fn serve_clients<RequestStreamType, ServiceParamType, ServiceFutureType, ServiceFuncType>(
    mock_handles: MockHandles,
    service_func: ServiceFuncType,
    service_param: ServiceParamType,
) -> Result<(), Error>
where
    RequestStreamType: fidl::endpoints::RequestStream,
    <RequestStreamType as fidl::endpoints::RequestStream>::Protocol:
        fidl::endpoints::DiscoverableProtocolMarker,
    ServiceParamType: Clone,
    ServiceFutureType: Future<Output = ()>,
    ServiceFuncType: (Fn(ServiceParamType, RequestStreamType) -> ServiceFutureType),
{
    let mut fs = fserver::ServiceFs::new();
    fs.dir("svc").add_fidl_service(|stream| stream);
    fs.serve_connection(mock_handles.outgoing_dir.into_channel())?;
    fs.for_each(|stream| service_func(service_param.clone(), stream)).await;
    Ok(())
}

/// Implements `TestRealComponent` for a mock.
///
/// Requires that the mock has a `moniker` field, and a `serve_one_client()`
/// method.
///
/// If the component is instantiated multiple times, the instances of the
/// component may share mutable state, depending on the `Clone` semantics of
/// `$component_type`.
macro_rules! impl_test_realm_component {
    ($component_type:ty) => {
        #[async_trait::async_trait]
        impl crate::traits::test_realm_component::TestRealmComponent for $component_type {
            fn moniker(&self) -> &Moniker {
                &self.moniker
            }

            async fn add_to_builder(&self, builder: &RealmBuilder) {
                let slf = self.clone();
                builder
                    .add_mock_child(
                        self.moniker().clone(),
                        move |mock_handles| {
                            Box::pin(crate::mocks::serve_clients(
                                mock_handles,
                                Self::serve_one_client,
                                slf.clone(),
                            ))
                        },
                        ChildOptions::new(),
                    )
                    .await
                    .unwrap();
            }
        }
    };
}

pub(crate) mod factory_reset_mock;
pub(crate) mod pointer_injector_mock;
pub(crate) mod sound_player_mock;
