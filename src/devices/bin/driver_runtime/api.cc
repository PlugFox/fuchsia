// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/fdf/arena.h>
#include <lib/fdf/channel.h>
#include <lib/fdf/dispatcher.h>
#include <lib/fdf/internal.h>

#include "src/devices/bin/driver_runtime/arena.h"
#include "src/devices/bin/driver_runtime/channel.h"
#include "src/devices/bin/driver_runtime/dispatcher.h"
#include "src/devices/bin/driver_runtime/driver_context.h"
#include "src/devices/bin/driver_runtime/handle.h"

// fdf_arena_t interface

__EXPORT fdf_status_t fdf_arena_create(uint32_t options, const char* tag, size_t tag_len,
                                       fdf_arena_t** out_arena) {
  return fdf_arena::Create(options, tag, tag_len, out_arena);
}

__EXPORT void* fdf_arena_allocate(fdf_arena_t* arena, size_t bytes) {
  return arena->Allocate(bytes);
}

__EXPORT void* fdf_arena_free(fdf_arena_t* arena, void* data) { return arena->Free(data); }

__EXPORT bool fdf_arena_contains(fdf_arena_t* arena, const void* data, size_t num_bytes) {
  return arena->Contains(data, num_bytes);
}

__EXPORT void fdf_arena_destroy(fdf_arena_t* arena) { arena->Destroy(); }

// fdf_channel_t interface

__EXPORT
fdf_status_t fdf_channel_create(uint32_t options, fdf_handle_t* out0, fdf_handle_t* out1) {
  return driver_runtime::Channel::Create(options, out0, out1);
}

__EXPORT
fdf_status_t fdf_channel_write(fdf_handle_t channel_handle, uint32_t options, fdf_arena_t* arena,
                               void* data, uint32_t num_bytes, zx_handle_t* handles,
                               uint32_t num_handles) {
  fbl::RefPtr<driver_runtime::Channel> channel;
  fdf_status_t status =
      driver_runtime::Handle::GetObject<driver_runtime::Channel>(channel_handle, &channel);
  // TODO(fxbug.dev/87046): we may want to consider killing the process.
  ZX_ASSERT(status == ZX_OK);
  return channel->Write(options, arena, data, num_bytes, handles, num_handles);
}

__EXPORT
fdf_status_t fdf_channel_read(fdf_handle_t channel_handle, uint32_t options, fdf_arena_t** arena,
                              void** data, uint32_t* num_bytes, zx_handle_t** handles,
                              uint32_t* num_handles) {
  fbl::RefPtr<driver_runtime::Channel> channel;
  fdf_status_t status =
      driver_runtime::Handle::GetObject<driver_runtime::Channel>(channel_handle, &channel);
  // TODO(fxbug.dev/87046): we may want to consider killing the process.
  ZX_ASSERT(status == ZX_OK);
  return channel->Read(options, arena, data, num_bytes, handles, num_handles);
}

__EXPORT
fdf_status_t fdf_channel_wait_async(struct fdf_dispatcher* dispatcher,
                                    fdf_channel_read_t* channel_read, uint32_t options) {
  if (!channel_read) {
    return ZX_ERR_INVALID_ARGS;
  }
  fbl::RefPtr<driver_runtime::Channel> channel;
  fdf_status_t status =
      driver_runtime::Handle::GetObject<driver_runtime::Channel>(channel_read->channel, &channel);
  // TODO(fxbug.dev/87046): we may want to consider killing the process.
  ZX_ASSERT(status == ZX_OK);
  return channel->WaitAsync(dispatcher, channel_read, options);
}

__EXPORT fdf_status_t fdf_channel_call(fdf_handle_t channel_handle, uint32_t options,
                                       zx_time_t deadline, const fdf_channel_call_args_t* args) {
  fbl::RefPtr<driver_runtime::Channel> channel;
  fdf_status_t status =
      driver_runtime::Handle::GetObject<driver_runtime::Channel>(channel_handle, &channel);
  // TODO(fxbug.dev/87046): we may want to consider killing the process.
  ZX_ASSERT(status == ZX_OK);
  return channel->Call(options, deadline, args);
}

__EXPORT void fdf_handle_close(fdf_handle_t channel_handle) {
  if (channel_handle == ZX_HANDLE_INVALID) {
    return;
  }
  driver_runtime::Handle* handle = driver_runtime::Handle::MapValueToHandle(channel_handle);
  // TODO(fxbug.dev/87046): we may want to consider killing the process.
  ZX_ASSERT(handle);

  fbl::RefPtr<driver_runtime::Channel> channel;
  fdf_status_t status = handle->GetObject<driver_runtime::Channel>(&channel);
  if (status != ZX_OK) {
    return;
  }
  channel->Close();
  // Drop the handle.
  handle->TakeOwnership();
}

// fdf_dispatcher_t interface
__EXPORT fdf_status_t fdf_dispatcher_create(uint32_t options, const char* scheduler_role,
                                            size_t scheduler_role_len,
                                            fdf_dispatcher_t** out_dispatcher) {
  std::unique_ptr<driver_runtime::Dispatcher> dispatcher;
  fdf_status_t status =
      driver_runtime::Dispatcher::Create(options, scheduler_role, scheduler_role_len, &dispatcher);
  if (status != ZX_OK) {
    return status;
  }
  *out_dispatcher = static_cast<fdf_dispatcher_t*>(dispatcher.release());
  return ZX_OK;
}

__EXPORT async_dispatcher_t* fdf_dispatcher_get_async_dispatcher(fdf_dispatcher_t* dispatcher) {
  return dispatcher->GetAsyncDispatcher();
}

__EXPORT fdf_dispatcher_t* fdf_dispatcher_from_async_dispatcher(async_dispatcher_t* dispatcher) {
  return static_cast<fdf_dispatcher*>(fdf_dispatcher::FromAsyncDispatcher(dispatcher));
}

__EXPORT void fdf_dispatcher_destroy(fdf_dispatcher_t* dispatcher) { return dispatcher->Destroy(); }

__EXPORT void fdf_internal_push_driver(const void* driver) { driver_context::PushDriver(driver); }

__EXPORT void fdf_internal_pop_driver() { driver_context::PopDriver(); }
