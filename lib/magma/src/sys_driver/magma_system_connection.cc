// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "magma_system_connection.h"
#include "magma_system_device.h"
#include "magma_util/macros.h"

MagmaSystemConnection::MagmaSystemConnection(std::weak_ptr<MagmaSystemDevice> weak_device,
                                             msd_connection_unique_ptr_t msd_connection,
                                             uint32_t capabilities)
    : device_(weak_device), msd_connection_(std::move(msd_connection))
{
    DASSERT(msd_connection_);

    has_display_capability_ = capabilities & MAGMA_SYSTEM_CAPABILITY_DISPLAY;
    has_render_capability_ = capabilities & MAGMA_SYSTEM_CAPABILITY_RENDERING;

    // should already be enforced in MagmaSystemDevice
    DASSERT(has_render_capability_ || has_display_capability_);
    DASSERT((capabilities &
             ~(MAGMA_SYSTEM_CAPABILITY_DISPLAY | MAGMA_SYSTEM_CAPABILITY_RENDERING)) == 0);
}

MagmaSystemConnection::~MagmaSystemConnection()
{
    auto device = device_.lock();
    if (device)
        device->ConnectionClosed(std::this_thread::get_id());
}

uint32_t MagmaSystemConnection::GetDeviceId()
{
    auto device = device_.lock();
    return device ? device->GetDeviceId() : 0;
}

bool MagmaSystemConnection::CreateContext(uint32_t context_id)
{
    if (!has_render_capability_)
        return DRETF(false, "Attempting to create a context without render capability");

    auto iter = context_map_.find(context_id);
    if (iter != context_map_.end())
        return DRETF(false, "Attempting to add context with duplicate id");

    auto msd_ctx = msd_connection_create_context(msd_connection());
    if (!msd_ctx)
        return DRETF(false, "Failed to create msd context");

    auto ctx = std::unique_ptr<MagmaSystemContext>(
        new MagmaSystemContext(this, msd_context_unique_ptr_t(msd_ctx, &msd_context_destroy)));

    context_map_.insert(std::make_pair(context_id, std::move(ctx)));
    return true;
}

bool MagmaSystemConnection::DestroyContext(uint32_t context_id)
{
    if (!has_render_capability_)
        return DRETF(false, "Attempting to destroy a context without render capability");

    auto iter = context_map_.find(context_id);
    if (iter == context_map_.end())
        return DRETF(false, "MagmaSystemConnection:Attempting to destroy invalid context id");
    context_map_.erase(iter);
    return true;
}

MagmaSystemContext* MagmaSystemConnection::LookupContext(uint32_t context_id)
{
    if (!has_render_capability_)
        return DRETP(nullptr, "Attempting to look up a context without render capability");

    auto iter = context_map_.find(context_id);
    if (iter == context_map_.end())
        return DRETP(nullptr, "MagmaSystemConnection: Attempting to lookup invalid context id");

    return iter->second.get();
}

bool MagmaSystemConnection::ExecuteCommandBuffer(uint64_t command_buffer_id, uint32_t context_id)
{
    if (!has_render_capability_)
        return DRETF(false, "Attempting to execute a command buffer without render capability");

    auto command_buffer = LookupBuffer(command_buffer_id);
    if (!command_buffer)
        return DRETF(false, "Attempting to execute invalid command buffer id");

    auto context = LookupContext(context_id);
    if (!context)
        return DRETF(false, "Attempting to execute command buffer on invalid context");

    if (!context->ExecuteCommandBuffer(command_buffer))
        return DRETF(false, "Context failed to execute command buffer");

    return true;
}

bool MagmaSystemConnection::WaitRendering(uint64_t buffer_id)
{
    if (!has_render_capability_)
        return DRETF(false, "Attempting to wait rendering without render capability");

    std::shared_ptr<MagmaSystemBuffer> system_buffer = LookupBuffer(buffer_id);
    if (!system_buffer)
        return DRETF(false, "Couldn't find system buffer for id 0x%lx", buffer_id);

    magma_status_t result =
        msd_connection_wait_rendering(msd_connection(), system_buffer->msd_buf());
    if (result != MAGMA_STATUS_OK)
        return DRETF(false, "msd_connection_wait_rendering failed: %d", result);

    return true;
}

bool MagmaSystemConnection::ImportBuffer(uint32_t handle, uint64_t* id_out)
{
    auto device = device_.lock();
    if (!device)
        return DRETF(false, "failed to lock device");

    auto buf = device->GetBufferForHandle(handle);
    if (!buf)
        return DRETF(false, "failed to get buffer for handle");

    uint64_t id = buf->id();

    auto iter = buffer_map_.find(id);
    if (iter == buffer_map_.end())
        buffer_map_.insert(std::make_pair(id, buf));

    *id_out = id;
    return true;
}

bool MagmaSystemConnection::ReleaseBuffer(uint64_t id)
{
    auto device = device_.lock();
    if (!device)
        return DRETF(false, "failed to lock device");

    auto iter = buffer_map_.find(id);
    if (iter == buffer_map_.end())
        return DRETF(false, "Attempting to free invalid buffer id");

    buffer_map_.erase(iter);
    // Now that our shared reference has been dropped we tell our
    // device that we're done with the buffer
    device->ReleaseBuffer(id);

    return true;
}

std::shared_ptr<MagmaSystemBuffer> MagmaSystemConnection::LookupBuffer(uint64_t id)
{
    auto iter = buffer_map_.find(id);
    if (iter == buffer_map_.end())
        return DRETP(nullptr, "Attempting to lookup invalid buffer id");

    return iter->second;
}

void MagmaSystemConnection::PageFlip(uint64_t id, magma_system_pageflip_callback_t callback,
                                     void* data)
{
    if (!has_display_capability_) {
        callback(DRET_MSG(MAGMA_STATUS_ACCESS_DENIED,
                          "Attempting to pageflip without display capability"),
                 data);
        return;
    }

    auto buf = LookupBuffer(id);
    if (!buf) {
        callback(DRET_MSG(MAGMA_STATUS_INVALID_ARGS, "Attempting to page flip with invalid buffer"),
                 data);
        return;
    }

    auto device = device_.lock();
    if (!device) {
        callback(DRET_MSG(-EINVAL, "Attempting to page flip, failed to lock device"), data);
        return;
    }

    device->PageFlip(buf, callback, data);
}
