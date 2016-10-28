// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "command_buffer.h"
#include "engine_command_streamer.h"
#include "msd_intel_context.h"

CommandBuffer::~CommandBuffer()
{
    if (!prepared_to_execute_)
        return;

    UnmapResourcesGpu();

    if (sequence_number_ != Sequencer::kInvalidSequenceNumber) {
        for (auto res : exec_resources_) {
            // only want to reset seq num on buffers that arent referenced
            // from another more recent command buffer
            if (res.buffer->sequence_number() == sequence_number_) {
                res.buffer->SetSequenceNumber(Sequencer::kInvalidSequenceNumber);
            }
        }
    }
}

void CommandBuffer::SetSequenceNumber(uint32_t sequence_number)
{
    sequence_number_ = sequence_number;
    for (auto res : exec_resources_) {
        res.buffer->SetSequenceNumber(sequence_number);
    }
}

CommandBuffer::CommandBuffer(magma_system_command_buffer* cmd_buf, msd_buffer** exec_buffers,
                             std::weak_ptr<ClientContext> context)
    : cmd_buf_(cmd_buf), context_(context)
{
    exec_resources_.reserve(cmd_buf->num_resources);
    for (uint32_t i = 0; i < cmd_buf->num_resources; i++) {
        auto buffer = MsdIntelAbiBuffer::cast(exec_buffers[i])->ptr();
        exec_resources_.push_back(
            ExecResource{buffer, cmd_buf->resources[i].offset, cmd_buf->resources[i].length});
    }

    prepared_to_execute_ = false;
}

MsdIntelContext* CommandBuffer::GetContext()
{
    DASSERT(prepared_to_execute_);
    return locked_context_.get();
}

bool CommandBuffer::GetGpuAddress(AddressSpaceId address_space_id, gpu_addr_t* gpu_addr_out)
{
    DASSERT(prepared_to_execute_);
    if (address_space_id != locked_context_->exec_address_space()->id())
        return DRETF(false, "wrong address space");
    *gpu_addr_out = batch_buffer_gpu_addr_;
    return true;
}

void CommandBuffer::UnmapResourcesGpu() { exec_resource_mappings_.clear(); }

bool CommandBuffer::PrepareForExecution(EngineCommandStreamer* engine)
{
    DASSERT(engine);

    locked_context_ = context_.lock();
    if (!locked_context_)
        return DRETF(false, "context has already been deleted, aborting");

    auto address_space = locked_context_->exec_address_space();

    if (!locked_context_->IsInitializedForEngine(engine->id())) {
        if (!engine->InitContext(locked_context_.get()))
            return DRETF(false, "failed to intialize context");
    }

    if (!locked_context_->Map(address_space, engine->id()))
        return DRETF(false, "failed to map context");

    exec_resource_mappings_.clear();
    exec_resource_mappings_.reserve(exec_resources_.size());

    if (!MapResourcesGpu(address_space, exec_resource_mappings_))
        return DRETF(false, "failed to map execution resources");

    if (!PatchRelocations(exec_resource_mappings_))
        return DRETF(false, "failed to patch relocations");

    batch_buffer_gpu_addr_ =
        exec_resource_mappings_[cmd_buf_->batch_buffer_resource_index]->gpu_addr();

    prepared_to_execute_ = true;
    engine_id_ = engine->id();

    return true;
}

bool CommandBuffer::MapResourcesGpu(std::shared_ptr<AddressSpace> address_space,
                                    std::vector<std::shared_ptr<GpuMapping>>& mappings)
{
    for (auto res : exec_resources_) {
        std::shared_ptr<GpuMapping> mapping = AddressSpace::GetSharedGpuMapping(
            address_space, res.buffer, res.offset, res.length, PAGE_SIZE);
        if (!mapping)
            return DRETF(false, "failed to map resource into GPU address space");
        mappings.push_back(mapping);
    }
    return true;
}

bool CommandBuffer::PatchRelocation(magma_system_relocation_entry* relocation,
                                    ExecResource* exec_resource, gpu_addr_t target_gpu_address)
{
    DLOG("PatchRelocation offset 0x%x exec_resource offset 0x%lx target_gpu_address 0x%lx "
         "target_offset 0x%x",
         relocation->offset, exec_resource->offset, target_gpu_address, relocation->target_offset);

    uint64_t dst_offset = exec_resource->offset + relocation->offset;

    uint32_t reloc_page_index = dst_offset >> PAGE_SHIFT;
    uint32_t offset_in_page = dst_offset & (PAGE_SIZE - 1);

    DLOG("reloc_page_index 0x%x offset_in_page 0x%x", reloc_page_index, offset_in_page);

    void* reloc_page_cpu_addr;
    if (!exec_resource->buffer->platform_buffer()->MapPageCpu(reloc_page_index,
                                                              &reloc_page_cpu_addr))
        return DRETF(false, "failed to map relocation page into CPU address space");

    DASSERT(reloc_page_cpu_addr);

    gpu_addr_t address_to_patch = target_gpu_address + relocation->target_offset;

    DASSERT(offset_in_page % sizeof(uint32_t) == 0); // just to be sure

    // actually patch the relocation
    static_cast<uint32_t*>(reloc_page_cpu_addr)[offset_in_page / sizeof(uint32_t)] =
        magma::lower_32_bits(address_to_patch);

    offset_in_page += sizeof(uint32_t);

    static_cast<uint32_t*>(reloc_page_cpu_addr)[offset_in_page / sizeof(uint32_t)] =
        magma::upper_32_bits(address_to_patch);

    // unmap the mapped page
    if (!exec_resource->buffer->platform_buffer()->UnmapPageCpu(reloc_page_index))
        return DRETF(false, "failed to unmap relocation page from CPU address space");

    return true;
}

bool CommandBuffer::PatchRelocations(std::vector<std::shared_ptr<GpuMapping>>& mappings)
{
    DASSERT(mappings.size() == cmd_buf_->num_resources);

    for (uint32_t res_index = 0; res_index < cmd_buf_->num_resources; res_index++) {
        auto resource = &cmd_buf_->resources[res_index];
        for (uint32_t reloc_index = 0; reloc_index < resource->num_relocations; reloc_index++) {
            auto reloc = &resource->relocations[reloc_index];
            DLOG("Patching relocation res_index %u reloc_index %u target_resource_index %u",
                 res_index, reloc_index, reloc->target_resource_index);
            auto& mapping = mappings[reloc->target_resource_index];
            if (!PatchRelocation(reloc, &exec_resources_[res_index], mapping->gpu_addr()))
                return DRETF(false, "failed to patch relocation");
        }
    }

    return true;
}