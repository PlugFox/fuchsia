// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/fidl/llcpp/client_base.h>
#include <lib/fidl/trace.h>
#include <lib/fidl/txn_header.h>
#include <lib/fit/function.h>
#include <stdio.h>

namespace fidl {
namespace internal {

// TODO(madhaviyengar): Move this constant to zircon/fidl.h
constexpr uint32_t kUserspaceTxidMask = 0x7FFFFFFF;

void ClientBase::Bind(std::shared_ptr<ClientBase> client, fidl::internal::AnyTransport transport,
                      async_dispatcher_t* dispatcher, AnyIncomingEventDispatcher&& event_dispatcher,
                      AnyTeardownObserver&& teardown_observer, ThreadingPolicy threading_policy) {
  ZX_DEBUG_ASSERT(!binding_.lock());
  ZX_DEBUG_ASSERT(client.get() == this);
  auto binding = AsyncClientBinding::Create(
      dispatcher, std::make_shared<fidl::internal::AnyTransport>(std::move(transport)),
      std::move(client), event_dispatcher->event_handler(), std::move(teardown_observer),
      threading_policy);
  binding_ = binding;
  dispatcher_ = dispatcher;
  event_dispatcher_ = std::move(event_dispatcher);
  binding->BeginFirstWait();
}

void ClientBase::AsyncTeardown() {
  if (auto binding = binding_.lock())
    binding->StartTeardown(std::move(binding));
}

void ClientBase::PrepareAsyncTxn(ResponseContext* context) {
  std::scoped_lock lock(lock_);

  // Generate the next txid. Verify that it doesn't overlap with any outstanding txids.
  do {
    do {
      context->txid_ = ++txid_base_ & kUserspaceTxidMask;  // txid must be within mask.
    } while (unlikely(!context->txid_));                   // txid must be non-zero.
  } while (unlikely(!contexts_.insert_or_find(context)));

  list_add_tail(&delete_list_, context);
}

void ClientBase::ForgetAsyncTxn(ResponseContext* context) {
  std::scoped_lock lock(lock_);

  ZX_ASSERT(context->InContainer());
  contexts_.erase(*context);
  list_delete(static_cast<list_node_t*>(context));
}

void ClientBase::ReleaseResponseContexts(fidl::UnbindInfo info) {
  // Release ownership on any outstanding |ResponseContext|s outside of locks.
  list_node_t delete_list;
  {
    std::scoped_lock lock(lock_);
    contexts_.clear();
    list_move(&delete_list_, &delete_list);
  }

  list_node_t* node = nullptr;
  list_node_t* temp_node = nullptr;
  list_for_every_safe(&delete_list, node, temp_node) {
    list_delete(node);
    auto* context = static_cast<ResponseContext*>(node);
    // Depending on what kind of error caused teardown, we may want to propgate
    // the error to all other outstanding contexts.
    switch (info.reason()) {
      case fidl::Reason::kClose:
        // |kClose| is never used on the client side.
        __builtin_abort();
        break;
      case fidl::Reason::kUnbind:
        // The user explicitly initiated teardown.
      case fidl::Reason::kEncodeError:
      case fidl::Reason::kDecodeError:
        // These errors are specific to one call, whose corresponding context
        // would have been notified during |Dispatch| or making the call.
        context->OnError(fidl::Result::Unbound());
        break;
      case fidl::Reason::kPeerClosed:
      case fidl::Reason::kDispatcherError:
      case fidl::Reason::kTransportError:
      case fidl::Reason::kUnexpectedMessage:
        // These errors apply to all calls.
        context->OnError(info.ToError());
        break;
    }
  }
}

void ClientBase::SendTwoWay(fidl::OutgoingMessage& message, ResponseContext* context,
                            const fidl::WriteOptions& write_options) {
  if (auto transport = GetTransport()) {
    PrepareAsyncTxn(context);
    message.set_txid(context->Txid());
    message.Write(*transport, write_options);
    if (!message.ok()) {
      ForgetAsyncTxn(context);
      TryAsyncDeliverError(message.error(), context);
      HandleSendError(message.error());
    }
    return;
  }
  TryAsyncDeliverError(fidl::Result::Unbound(), context);
}

fidl::Result ClientBase::SendOneWay(::fidl::OutgoingMessage& message,
                                    const fidl::WriteOptions& write_options) {
  if (auto transport = GetTransport()) {
    message.set_txid(0);
    message.Write(*transport, write_options);
    if (!message.ok()) {
      HandleSendError(message.error());
      return message.error();
    }
    return fidl::Result::Ok();
  }
  return fidl::Result::Unbound();
}

void ClientBase::HandleSendError(fidl::Result error) {
  if (auto binding = binding_.lock()) {
    binding->HandleError(std::move(binding), {UnbindInfo{error}, ErrorOrigin::kSend});
  }
}

void ClientBase::TryAsyncDeliverError(::fidl::Result error, ResponseContext* context) {
  zx_status_t status = context->TryAsyncDeliverError(error, dispatcher_);
  if (status != ZX_OK) {
    context->OnError(error);
  }
}

std::optional<UnbindInfo> ClientBase::Dispatch(
    fidl::IncomingMessage& msg, internal::IncomingTransportContext* transport_context) {
  if (fit::nullable epitaph = msg.maybe_epitaph(); unlikely(epitaph)) {
    return UnbindInfo::PeerClosed((*epitaph)->error);
  }

  auto* hdr = msg.header();
  if (hdr->txid == 0) {
    // Dispatch events (received messages with no txid).
    return event_dispatcher_->DispatchEvent(msg, transport_context);
  }

  // If this is a response, look up the corresponding ResponseContext based on the txid.
  ResponseContext* context = nullptr;
  {
    std::scoped_lock lock(lock_);
    context = contexts_.erase(hdr->txid);
    if (likely(context != nullptr)) {
      list_delete(static_cast<list_node_t*>(context));
    } else {
      // Received unknown txid.
      return UnbindInfo{
          Result::UnexpectedMessage(ZX_ERR_NOT_FOUND, fidl::internal::kErrorUnknownTxId)};
    }
  }
  return context->OnRawResult(std::move(msg), transport_context);
}

void ClientController::Bind(std::shared_ptr<ClientBase>&& client_impl,
                            fidl::internal::AnyTransport client_end, async_dispatcher_t* dispatcher,
                            AnyIncomingEventDispatcher&& event_dispatcher,
                            AnyTeardownObserver&& teardown_observer,
                            ThreadingPolicy threading_policy) {
  ZX_ASSERT(!client_impl_);
  client_impl_ = std::move(client_impl);
  client_impl_->Bind(client_impl_, std::move(client_end), dispatcher, std::move(event_dispatcher),
                     std::move(teardown_observer), threading_policy);
  control_ = std::make_shared<ControlBlock>(client_impl_);
}

void ClientController::Unbind() {
  ZX_ASSERT(client_impl_);
  control_.reset();
  client_impl_->ClientBase::AsyncTeardown();
}

}  // namespace internal
}  // namespace fidl
