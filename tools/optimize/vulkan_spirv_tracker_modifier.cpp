/*
** Copyright (c) 2025 LunarG, Inc.
** Copyright (c) 2025 Arm Limited and/or its affiliates <open-source-office@arm.com>
**
** Permission is hereby granted, free of charge, to any person obtaining a
** copy of this software and associated documentation files (the "Software"),
** to deal in the Software without restriction, including without limitation
** the rights to use, copy, modify, merge, publish, distribute, sublicense,
** and/or sell copies of the Software, and to permit persons to whom the
** Software is furnished to do so, subject to the following conditions:
**
** The above copyright notice and this permission notice shall be included in
** all copies or substantial portions of the Software.
**
** THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
** IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
** FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
** AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
** LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
** FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
** DEALINGS IN THE SOFTWARE.
*/
#include <unordered_map>
#include <unordered_set>
#include <algorithm>
#include <iostream>

#include "vulkan_spirv_tracker_modifier.h"
#include "format/format.h"
#include "util/defines.h"
#include "util/memory_output_stream.h"
#include "encode/parameter_buffer.h"
#include "encode/struct_pointer_encoder.h"

GFXRECON_BEGIN_NAMESPACE(gfxrecon)
GFXRECON_BEGIN_NAMESPACE(decode)

VulkanSpirvTrackModifier::VulkanSpirvTrackModifier(bool verbose, bool error_on_buffers_incomplete) : m_verbose(verbose)
{
    if (error_on_buffers_incomplete)
    {
        m_flags |= ERROR_RAISE_ON_BUFFERS_INCOMPLETE;
    }
}

bool VulkanSpirvTrackModifier::CanOptimize()
{
    return true;
}

void VulkanSpirvTrackModifier::ProcessFillMemoryCommand(uint64_t       memory_id,
                                                        uint64_t       offset,
                                                        uint64_t       size,
                                                        const uint8_t* data)
{
    if (IsModificationPass())
    {
        return;
    }

    const auto memory_entry_iter = memory_binding_entries_.find(memory_id);

    // retrieve data from filled memory for each bound buffers in the memory.
    // the data is provided to simulator input_data's bindings later

    if (memory_entry_iter != memory_binding_entries_.end())
    {
        const DeviceMemoryInfo& mem_info = memory_entry_iter->second;
        for (const MemoryBindingRecord& binding : mem_info.memory_binding_records_)
        {
            if (!binding.isBuffer || buffer_entries_.find(binding.handle) == buffer_entries_.end())
            {
                continue;
            }

            VkDeviceSize buffer_offset_from_mapped_ptr = binding.offset - mem_info.mapping.offset;
            VkDeviceSize buffer_end_from_mapped_ptr =
                buffer_offset_from_mapped_ptr + buffer_entries_[binding.handle].size;
            uint64_t copy_size = 0;

            // case 1: the bound buffer starts from this memory block
            if ((buffer_offset_from_mapped_ptr >= offset) && (buffer_offset_from_mapped_ptr < (offset + size)))
            {
                // case 1.1: whole buffer in the memory range
                if (buffer_end_from_mapped_ptr < (offset + size))
                {
                    copy_size = buffer_entries_[binding.handle].size;
                }
                else // case 1.2: buffer exceeding memory range if it's bigger than page size and updated incontiguously
                {
                    copy_size = size - (buffer_offset_from_mapped_ptr - offset);
                }
                std::memcpy(buffer_entries_[binding.handle].data.data(),
                            data + (buffer_offset_from_mapped_ptr - offset),
                            copy_size);
            }
            // case 2: buffer starts from previous memory range
            else if (buffer_offset_from_mapped_ptr < offset && buffer_end_from_mapped_ptr > offset)
            {
                if (buffer_end_from_mapped_ptr <= (offset + size))
                {
                    // case 2.1: buffer ends in this memory range
                    copy_size = buffer_end_from_mapped_ptr - offset;
                }
                else
                {
                    // case 2.2: buffer exceeding the memory range
                    copy_size = size;
                }
                std::memcpy(buffer_entries_[binding.handle].data.data() + (offset - buffer_offset_from_mapped_ptr),
                            data,
                            copy_size);
            }
        }
    }
}

void VulkanSpirvTrackModifier::Process_vkCreateBuffer(const ApiCallInfo&                                   call_info,
                                                      VkResult                                             returnValue,
                                                      format::HandleId                                     device,
                                                      StructPointerDecoder<Decoded_VkBufferCreateInfo>*    pCreateInfo,
                                                      StructPointerDecoder<Decoded_VkAllocationCallbacks>* pAllocator,
                                                      HandlePointerDecoder<VkBuffer>*                      pBuffer)
{
    if (IsModificationPass())
    {
        return;
    }

    format::HandleId handle                   = *pBuffer->GetPointer();
    buffer_entries_[handle].handle            = handle;
    buffer_entries_[handle].size              = pCreateInfo->GetPointer()->size;
    buffer_entries_[handle].usage             = pCreateInfo->GetPointer()->usage;
    buffer_entries_[handle].flags             = pCreateInfo->GetPointer()->flags;
    buffer_entries_[handle].creation_index    = call_info.index;
    buffer_entries_[handle].destruction_index = UINT64_MAX;
    buffer_entries_[handle].data.resize(buffer_entries_[handle].size);
}

void VulkanSpirvTrackModifier::Process_vkDestroyBuffer(const ApiCallInfo&                                   call_info,
                                                       format::HandleId                                     device,
                                                       format::HandleId                                     buffer,
                                                       StructPointerDecoder<Decoded_VkAllocationCallbacks>* pAllocator)
{
    if (IsModificationPass())
    {
        return;
    }
    if (buffer_entries_.find(buffer) != buffer_entries_.end())
    {
        buffer_entries_[buffer].destruction_index = call_info.index;
    }
}

void VulkanSpirvTrackModifier::Process_vkAllocateMemory(
    const ApiCallInfo&                                   call_info,
    VkResult                                             returnValue,
    format::HandleId                                     device,
    StructPointerDecoder<Decoded_VkMemoryAllocateInfo>*  pAllocateInfo,
    StructPointerDecoder<Decoded_VkAllocationCallbacks>* pAllocator,
    HandlePointerDecoder<VkDeviceMemory>*                pMemory)
{
    if (IsModificationPass())
    {
        return;
    }

    format::HandleId handle                           = *pMemory->GetPointer();
    memory_binding_entries_[handle].handle            = handle;
    memory_binding_entries_[handle].size              = pAllocateInfo->GetPointer()->allocationSize;
    memory_binding_entries_[handle].type_index        = pAllocateInfo->GetPointer()->memoryTypeIndex;
    memory_binding_entries_[handle].creation_index    = call_info.index;
    memory_binding_entries_[handle].destruction_index = UINT64_MAX;

    const auto* meta_flags_info =
        GetPNextMetaStruct<Decoded_VkMemoryAllocateFlagsInfo>(pAllocateInfo->GetMetaStructPointer()->pNext);
    if (meta_flags_info)
    {
        memory_binding_entries_[handle].flags = meta_flags_info->decoded_value->flags;
    }
}

void VulkanSpirvTrackModifier::Process_vkFreeMemory(const ApiCallInfo&                                   call_info,
                                                    format::HandleId                                     device,
                                                    format::HandleId                                     memory,
                                                    StructPointerDecoder<Decoded_VkAllocationCallbacks>* pAllocator)
{
    if (IsModificationPass())
    {
        return;
    }

    if (memory_binding_entries_.find(memory) != memory_binding_entries_.end())
    {
        memory_binding_entries_[memory].destruction_index = call_info.index;
    }
}

void VulkanSpirvTrackModifier::Process_vkBindBufferMemory(const ApiCallInfo& call_info,
                                                          VkResult           returnValue,
                                                          format::HandleId   device,
                                                          format::HandleId   buffer,
                                                          format::HandleId   memory,
                                                          VkDeviceSize       memory_offset)
{
    if (IsModificationPass())
    {
        return;
    }

    if (memory_binding_entries_.find(memory) != memory_binding_entries_.end())
    {
        memory_binding_entries_[memory].memory_binding_records_.push_back({ buffer, true, memory_offset });
    }
}

void VulkanSpirvTrackModifier::Process_vkBindBufferMemory2(
    const ApiCallInfo&                                    call_info,
    VkResult                                              returnValue,
    format::HandleId                                      device,
    uint32_t                                              bindInfoCount,
    StructPointerDecoder<Decoded_VkBindBufferMemoryInfo>* pBindInfos)
{
    if (IsModificationPass())
    {
        return;
    }

    const auto* bind_infos      = pBindInfos->GetPointer();
    const auto* meta_bind_infos = pBindInfos->GetMetaStructPointer();

    for (uint32_t i = 0; i < bindInfoCount; ++i)
    {
        if (memory_binding_entries_.find(meta_bind_infos[i].memory) != memory_binding_entries_.end())
        {
            memory_binding_entries_[meta_bind_infos[i].memory].memory_binding_records_.push_back(
                { meta_bind_infos[i].buffer, true, bind_infos[i].memoryOffset });
        }
    }
}

void VulkanSpirvTrackModifier::Process_vkMapMemory(const ApiCallInfo&               call_info,
                                                   VkResult                         returnValue,
                                                   format::HandleId                 device,
                                                   format::HandleId                 memory,
                                                   VkDeviceSize                     offset,
                                                   VkDeviceSize                     size,
                                                   VkMemoryMapFlags                 flags,
                                                   PointerDecoder<uint64_t, void*>* ppData)
{
    if (IsModificationPass())
    {
        return;
    }

    if (memory_binding_entries_.find(memory) != memory_binding_entries_.end())
    {
        if (size == VK_WHOLE_SIZE)
        {
            GFXRECON_ASSERT(offset <= memory_binding_entries_[memory].size);
            size = memory_binding_entries_[memory].size - offset;
        }

        memory_binding_entries_[memory].mapping.offset        = offset;
        memory_binding_entries_[memory].mapping.size          = size;
        memory_binding_entries_[memory].mapping.map_memory    = *ppData->GetPointer();
        memory_binding_entries_[memory].mapping.shadow_memory = 0;
    }
}

void VulkanSpirvTrackModifier::Process_vkMapMemory2(const ApiCallInfo&                             call_info,
                                                    VkResult                                       returnValue,
                                                    format::HandleId                               device,
                                                    StructPointerDecoder<Decoded_VkMemoryMapInfo>* pMemoryMapInfo,
                                                    PointerDecoder<uint64_t, void*>*               ppData)
{
    if (IsModificationPass())
    {
        return;
    }

    const auto* map_info      = pMemoryMapInfo->GetPointer();
    const auto* meta_map_info = pMemoryMapInfo->GetMetaStructPointer();

    format::HandleId memory = meta_map_info->memory;
    if (memory_binding_entries_.find(memory) != memory_binding_entries_.end())
    {
        VkDeviceSize in_size = map_info->size;
        if (map_info->size == VK_WHOLE_SIZE)
        {
            GFXRECON_ASSERT(map_info->offset <= memory_binding_entries_[memory].size);
            in_size = memory_binding_entries_[memory].size - map_info->offset;
        }

        memory_binding_entries_[memory].mapping.offset        = map_info->offset;
        memory_binding_entries_[memory].mapping.size          = in_size;
        memory_binding_entries_[memory].mapping.map_memory    = *ppData->GetPointer();
        memory_binding_entries_[memory].mapping.shadow_memory = 0;
    }
}

void VulkanSpirvTrackModifier::Process_vkUnmapMemory(const ApiCallInfo& call_info,
                                                     format::HandleId   device,
                                                     format::HandleId   memory)
{
    if (IsModificationPass())
    {
        return;
    }

    if (memory_binding_entries_.find(memory) != memory_binding_entries_.end())
    {
        memory_binding_entries_[memory].mapping.offset        = 0;
        memory_binding_entries_[memory].mapping.size          = 0;
        memory_binding_entries_[memory].mapping.map_memory    = 0;
        memory_binding_entries_[memory].mapping.shadow_memory = 0;
    }
}

void VulkanSpirvTrackModifier::Process_vkUnmapMemory2(const ApiCallInfo&                               call_info,
                                                      VkResult                                         returnValue,
                                                      format::HandleId                                 device,
                                                      StructPointerDecoder<Decoded_VkMemoryUnmapInfo>* pMemoryUnmapInfo)
{
    if (IsModificationPass())
    {
        return;
    }

    const auto* meta_unmap_info = pMemoryUnmapInfo->GetMetaStructPointer();

    format::HandleId memory = meta_unmap_info->memory;
    if (memory_binding_entries_.find(memory) != memory_binding_entries_.end())
    {
        memory_binding_entries_[memory].mapping.offset        = 0;
        memory_binding_entries_[memory].mapping.size          = 0;
        memory_binding_entries_[memory].mapping.map_memory    = 0;
        memory_binding_entries_[memory].mapping.shadow_memory = 0;
    }
}

void VulkanSpirvTrackModifier::Process_vkCreateAccelerationStructureKHR(
    const ApiCallInfo&                                                  call_info,
    VkResult                                                            returnValue,
    format::HandleId                                                    device,
    StructPointerDecoder<Decoded_VkAccelerationStructureCreateInfoKHR>* pCreateInfo,
    StructPointerDecoder<Decoded_VkAllocationCallbacks>*                pAllocator,
    HandlePointerDecoder<VkAccelerationStructureKHR>*                   pAccelerationStructure)
{
    if (IsModificationPass())
    {
        return;
    }

    format::HandleId handle = *pAccelerationStructure->GetPointer();

    acceleration_structure_entries_[handle].handle            = handle;
    acceleration_structure_entries_[handle].buf_handle        = pCreateInfo->GetMetaStructPointer()->buffer;
    acceleration_structure_entries_[handle].size              = pCreateInfo->GetPointer()->size;
    acceleration_structure_entries_[handle].offset            = pCreateInfo->GetPointer()->offset;
    acceleration_structure_entries_[handle].type              = pCreateInfo->GetPointer()->type;
    acceleration_structure_entries_[handle].creation_index    = call_info.index;
    acceleration_structure_entries_[handle].destruction_index = UINT64_MAX;

    VkDeviceAddress back_buffer_address = buffer_entries_[pCreateInfo->GetMetaStructPointer()->buffer].device_address;
    if (back_buffer_address != 0)
    {
        VkDeviceAddress address = back_buffer_address + pCreateInfo->GetPointer()->offset;
        acceleration_structure_entries_[handle].device_address = address;
        acceleration_structure_device_addresses_[address].insert(handle);
    }
}

void VulkanSpirvTrackModifier::Process_vkDestroyAccelerationStructureKHR(
    const ApiCallInfo&                                   call_info,
    format::HandleId                                     device,
    format::HandleId                                     accelerationStructure,
    StructPointerDecoder<Decoded_VkAllocationCallbacks>* pAllocator)
{
    if (IsModificationPass())
    {
        return;
    }
    if (acceleration_structure_entries_.find(accelerationStructure) != acceleration_structure_entries_.end())
    {
        acceleration_structure_entries_[accelerationStructure].destruction_index = call_info.index;
    }
}

void VulkanSpirvTrackModifier::Process_vkCreateShaderModule(
    const ApiCallInfo&                                      call_info,
    VkResult                                                returnValue,
    format::HandleId                                        device,
    StructPointerDecoder<Decoded_VkShaderModuleCreateInfo>* pCreateInfo,
    StructPointerDecoder<Decoded_VkAllocationCallbacks>*    pAllocator,
    HandlePointerDecoder<VkShaderModule>*                   pShaderModule)
{
    if (IsModificationPass())
    {
        return;
    }

    format::HandleId                handleId = *pShaderModule->GetPointer();
    const VkShaderModuleCreateInfo* info     = pCreateInfo->GetPointer();

    shader_module_entries_[handleId].handle            = handleId;
    shader_module_entries_[handleId].creation_index    = call_info.index;
    shader_module_entries_[handleId].destruction_index = UINT64_MAX;
    shader_module_entries_[handleId].codeSize          = info->codeSize;
    shader_module_entries_[handleId].pCode             = { info->pCode, info->pCode + info->codeSize / 4 };
}

void VulkanSpirvTrackModifier::Process_vkCreateDescriptorSetLayout(
    const ApiCallInfo&                                             call_info,
    VkResult                                                       returnValue,
    format::HandleId                                               device,
    StructPointerDecoder<Decoded_VkDescriptorSetLayoutCreateInfo>* pCreateInfo,
    StructPointerDecoder<Decoded_VkAllocationCallbacks>*           pAllocator,
    HandlePointerDecoder<VkDescriptorSetLayout>*                   pSetLayout)
{
    if (IsModificationPass())
    {
        return;
    }

    format::HandleId                       handle = *pSetLayout->GetPointer();
    const VkDescriptorSetLayoutCreateInfo* info   = pCreateInfo->GetPointer();

    const auto* meta_flag_info = GetPNextMetaStruct<Decoded_VkDescriptorSetLayoutBindingFlagsCreateInfo>(
        pCreateInfo->GetMetaStructPointer()->pNext);
    const auto* meta_mutable_type_info =
        GetPNextMetaStruct<Decoded_VkMutableDescriptorTypeCreateInfoEXT>(pCreateInfo->GetMetaStructPointer()->pNext);

    set_layout_entries_[handle].handle            = handle;
    set_layout_entries_[handle].creation_index    = call_info.index;
    set_layout_entries_[handle].destruction_index = UINT64_MAX;
    set_layout_entries_[handle].flags             = info->flags;

    for (uint32_t i = 0; i < info->bindingCount; i++)
    {
        Binding binding;
        binding.binding         = info->pBindings[i].binding;
        binding.type            = info->pBindings[i].descriptorType;
        binding.stageFlags      = info->pBindings[i].stageFlags;
        binding.descriptorCount = info->pBindings[i].descriptorCount;
        binding.binding_flags   = (meta_flag_info == nullptr) ? 0 : meta_flag_info->pBindingFlags.GetPointer()[i];

        if ((binding.type == VK_DESCRIPTOR_TYPE_MUTABLE_EXT) && (meta_mutable_type_info != nullptr) &&
            (meta_mutable_type_info->decoded_value != nullptr) &&
            (meta_mutable_type_info->pMutableDescriptorTypeLists != nullptr) &&
            (i < meta_mutable_type_info->decoded_value->mutableDescriptorTypeListCount))
        {
            const auto& mutable_type_list = meta_mutable_type_info->pMutableDescriptorTypeLists->GetPointer()[i];
            const auto& meta_mutable_type_list =
                meta_mutable_type_info->pMutableDescriptorTypeLists->GetMetaStructPointer()[i];

            for (uint32_t type_index = 0; type_index < mutable_type_list.descriptorTypeCount; ++type_index)
            {
                binding.mutable_descriptor_types.push_back(
                    meta_mutable_type_list.pDescriptorTypes.GetPointer()[type_index]);
            }
        }

        set_layout_entries_[handle].bindings[binding.binding] = binding;
    }
}

void VulkanSpirvTrackModifier::Process_vkDestroyDescriptorSetLayout(
    const ApiCallInfo&                                   call_info,
    format::HandleId                                     device,
    format::HandleId                                     descriptorSetLayout,
    StructPointerDecoder<Decoded_VkAllocationCallbacks>* pAllocator)
{
    if (IsModificationPass())
    {
        return;
    }
    if (set_layout_entries_.find(descriptorSetLayout) != set_layout_entries_.end())
    {
        set_layout_entries_[descriptorSetLayout].destruction_index = call_info.index;
    }
}

void VulkanSpirvTrackModifier::Process_vkGetDescriptorSetLayoutSizeEXT(const ApiCallInfo&            call_info,
                                                                       format::HandleId              device,
                                                                       format::HandleId              layout,
                                                                       PointerDecoder<VkDeviceSize>* pLayoutSizeInBytes)
{
    if (set_layout_entries_.find(layout) != set_layout_entries_.end())
    {
        set_layout_entries_[layout].size = *pLayoutSizeInBytes->GetPointer();
    }
}

void VulkanSpirvTrackModifier::Process_vkGetDescriptorSetLayoutBindingOffsetEXT(const ApiCallInfo&            call_info,
                                                                                format::HandleId              device,
                                                                                format::HandleId              layout,
                                                                                uint32_t                      binding,
                                                                                PointerDecoder<VkDeviceSize>* pOffset)
{
    if (set_layout_entries_.find(layout) != set_layout_entries_.end())
    {
        set_layout_entries_[layout].bindings[binding].offset = *pOffset->GetPointer();
    }
}

void VulkanSpirvTrackModifier::Process_vkCreateDescriptorPool(
    const ApiCallInfo&                                        call_info,
    VkResult                                                  returnValue,
    format::HandleId                                          device,
    StructPointerDecoder<Decoded_VkDescriptorPoolCreateInfo>* pCreateInfo,
    StructPointerDecoder<Decoded_VkAllocationCallbacks>*      pAllocator,
    HandlePointerDecoder<VkDescriptorPool>*                   pDescriptorPool)
{
    if (IsModificationPass())
    {
        return;
    }

    format::HandleId handle = *pDescriptorPool->GetPointer();

    descriptor_pool_entries_[handle].handle            = handle;
    descriptor_pool_entries_[handle].creation_index    = call_info.index;
    descriptor_pool_entries_[handle].destruction_index = UINT64_MAX;
}

void VulkanSpirvTrackModifier::Process_vkDestroyDescriptorPool(
    const ApiCallInfo&                                   call_info,
    format::HandleId                                     device,
    format::HandleId                                     descriptorPool,
    StructPointerDecoder<Decoded_VkAllocationCallbacks>* pAllocator)
{
    if (IsModificationPass())
    {
        return;
    }
    if (descriptor_pool_entries_.find(descriptorPool) != descriptor_pool_entries_.end())
    {
        descriptor_pool_entries_[descriptorPool].destruction_index = call_info.index;
    }
    for (auto id : descriptor_pool_entries_[descriptorPool].descriptorSets)
    {
        descriptor_set_entries_.at(id).destruction_index = call_info.index;
    }
}

void VulkanSpirvTrackModifier::Process_vkAllocateDescriptorSets(
    const ApiCallInfo&                                         call_info,
    VkResult                                                   returnValue,
    format::HandleId                                           device,
    StructPointerDecoder<Decoded_VkDescriptorSetAllocateInfo>* pAllocateInfo,
    HandlePointerDecoder<VkDescriptorSet>*                     pDescriptorSets)
{
    if (IsModificationPass())
    {
        return;
    }

    if (returnValue != VK_SUCCESS)
    {
        return;
    }

    const Decoded_VkDescriptorSetAllocateInfo* alloc_info = pAllocateInfo->GetMetaStructPointer();

    const auto* meta_count_info =
        GetPNextMetaStruct<Decoded_VkDescriptorSetVariableDescriptorCountAllocateInfoEXT>(alloc_info->pNext);

    for (uint32_t i = 0; i < alloc_info->decoded_value->descriptorSetCount; i++)
    {
        format::HandleId setLayout      = alloc_info->pSetLayouts.GetPointer()[i];
        format::HandleId descriptorSet  = pDescriptorSets->GetPointer()[i];
        format::HandleId descriptorPool = alloc_info->descriptorPool;

        if (descriptorSet == 0)
        {
            GFXRECON_LOG_INFO(
                "call %llu vkAllocateDescriptorSets: descriptorSet is 0. Maybe something is error during tracing.",
                call_info.index);
            continue;
        }

        descriptor_pool_entries_.at(descriptorPool).descriptorSets.push_back(descriptorSet);

        descriptor_set_entries_[descriptorSet].handle            = descriptorSet;
        descriptor_set_entries_[descriptorSet].creation_index    = call_info.index;
        descriptor_set_entries_[descriptorSet].destruction_index = UINT64_MAX;

        // filling binding
        for (const auto& layout_binding_iter : set_layout_entries_[setLayout].bindings)
        {
            const Binding& binding = layout_binding_iter.second;
            GFXRECON_ASSERT(descriptor_set_entries_[descriptorSet].binding_descriptor_array.find(binding.binding) ==
                            descriptor_set_entries_[descriptorSet].binding_descriptor_array.end());

            descriptor_set_entries_[descriptorSet].binding_descriptor_array[binding.binding].binding = binding.binding;
            descriptor_set_entries_[descriptorSet].binding_descriptor_array[binding.binding].type    = binding.type;
            descriptor_set_entries_[descriptorSet].binding_descriptor_array[binding.binding].mutable_descriptor_types =
                binding.mutable_descriptor_types;
            descriptor_set_entries_[descriptorSet].binding_descriptor_array[binding.binding].descriptorCount =
                binding.descriptorCount;
            descriptor_set_entries_[descriptorSet].binding_descriptor_array[binding.binding].stageFlags =
                binding.stageFlags;
            descriptor_set_entries_[descriptorSet].binding_descriptor_array[binding.binding].binding_flags =
                binding.binding_flags;

            if ((meta_count_info != nullptr) &&
                (binding.binding_flags & VK_DESCRIPTOR_BINDING_VARIABLE_DESCRIPTOR_COUNT_BIT_EXT) ==
                    VK_DESCRIPTOR_BINDING_VARIABLE_DESCRIPTOR_COUNT_BIT_EXT)
            {
                descriptor_set_entries_[descriptorSet].binding_descriptor_array[binding.binding].descriptorCount =
                    meta_count_info->pDescriptorCounts.GetPointer()[i];
            }
            if (binding.type == VK_DESCRIPTOR_TYPE_INLINE_UNIFORM_BLOCK)
            {
                descriptor_set_entries_[descriptorSet]
                    .binding_descriptor_array[binding.binding]
                    .inline_uniform_block_data.resize(binding.descriptorCount, 0);
                descriptor_set_entries_[descriptorSet]
                    .binding_descriptor_array[binding.binding]
                    .inline_uniform_block_written.resize(binding.descriptorCount, 0);
            }
        }
    }
}

void VulkanSpirvTrackModifier::Process_vkUpdateDescriptorSets(
    const ApiCallInfo&                                  call_info,
    format::HandleId                                    device,
    uint32_t                                            descriptorWriteCount,
    StructPointerDecoder<Decoded_VkWriteDescriptorSet>* pDescriptorWrites,
    uint32_t                                            descriptorCopyCount,
    StructPointerDecoder<Decoded_VkCopyDescriptorSet>*  pDescriptorCopies)
{
    if (IsModificationPass())
    {
        return;
    }

    auto binding_accepts_descriptor_type = [](const DescriptorArray& binding_array, VkDescriptorType descriptor_type) {
        if (binding_array.type == descriptor_type)
        {
            return true;
        }
        if (binding_array.type != VK_DESCRIPTOR_TYPE_MUTABLE_EXT)
        {
            return false;
        }
        for (VkDescriptorType mutable_type : binding_array.mutable_descriptor_types)
        {
            if (mutable_type == descriptor_type)
            {
                return true;
            }
        }
        return false;
    };

    for (uint32_t i = 0; i < descriptorWriteCount; i++)
    {
        const auto& write      = pDescriptorWrites->GetPointer()[i];
        const auto& meta_write = pDescriptorWrites->GetMetaStructPointer()[i];

        auto descriptor_set_iter = descriptor_set_entries_.find(meta_write.dstSet);
        GFXRECON_ASSERT(descriptor_set_iter != descriptor_set_entries_.end());

        auto binding_array_iter = descriptor_set_iter->second.binding_descriptor_array.find(write.dstBinding);
        GFXRECON_ASSERT(binding_array_iter != descriptor_set_iter->second.binding_descriptor_array.end());

        GFXRECON_ASSERT(binding_accepts_descriptor_type(binding_array_iter->second, write.descriptorType));

        uint32_t alloc_descriptor_count = binding_array_iter->second.descriptorCount;
        if ((write.descriptorType == VK_DESCRIPTOR_TYPE_STORAGE_BUFFER ||
             write.descriptorType == VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER ||
             write.descriptorType == VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC ||
             write.descriptorType == VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC) &&
            write.pBufferInfo)
        {
            for (uint32_t s = 0; s < write.descriptorCount; s++)
            {
                uint32_t array_index = write.dstArrayElement + s;

                const auto& meta_buffer_info = meta_write.pBufferInfo->GetMetaStructPointer()[s];
                if (meta_buffer_info.buffer == format::kNullHandleId)
                {
                    GFXRECON_ASSERT(meta_buffer_info.decoded_value->range == VK_WHOLE_SIZE);
                    GFXRECON_ASSERT(meta_buffer_info.decoded_value->offset == 0);
                }
                binding_array_iter->second.descriptor_entries[array_index] = {
                    .type   = write.descriptorType,
                    .buffer = { meta_buffer_info.buffer,
                                meta_buffer_info.decoded_value->range,
                                meta_buffer_info.decoded_value->offset }
                };
            }
        }
        else if (write.descriptorType == VK_DESCRIPTOR_TYPE_INLINE_UNIFORM_BLOCK)
        {
            const auto* meta_inline_uniform_block =
                GetPNextMetaStruct<Decoded_VkWriteDescriptorSetInlineUniformBlock>(meta_write.pNext);
            if ((meta_inline_uniform_block == nullptr) || (meta_inline_uniform_block->decoded_value == nullptr))
            {
                GFXRECON_LOG_INFO("call %llu vkUpdateDescriptorSets: missing VkWriteDescriptorSetInlineUniformBlock in "
                                  "pNext for binding %u.",
                                  call_info.index,
                                  write.dstBinding);
                continue;
            }

            const auto* inline_uniform_block = meta_inline_uniform_block->decoded_value;
            GFXRECON_ASSERT(inline_uniform_block->sType == VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET_INLINE_UNIFORM_BLOCK);

            uint32_t size        = write.descriptorCount;
            uint32_t offset      = write.dstArrayElement;
            auto&    inline_data = binding_array_iter->second.inline_uniform_block_data;
            GFXRECON_ASSERT(size == inline_uniform_block->dataSize);
            GFXRECON_ASSERT(size + offset <= inline_data.size());

            const uint8_t* src_data = meta_inline_uniform_block->pData.GetPointer();
            GFXRECON_ASSERT((size == 0) || (src_data != nullptr));

            util::platform::MemoryCopy(inline_data.data() + offset, size, src_data, size);
        }
        else if (write.descriptorType == VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR)
        {
            const auto* meta_structure =
                GetPNextMetaStruct<Decoded_VkWriteDescriptorSetAccelerationStructureKHR>(meta_write.pNext);
            if ((meta_structure == nullptr) || (meta_structure->decoded_value == nullptr))
            {
                GFXRECON_LOG_INFO(
                    "call %llu vkUpdateDescriptorSets: missing VkWriteDescriptorSetAccelerationStructureKHR in "
                    "pNext for binding %u.",
                    call_info.index,
                    write.dstBinding);
                continue;
            }

            const auto* structure = meta_structure->decoded_value;
            GFXRECON_ASSERT(structure->sType == VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET_ACCELERATION_STRUCTURE_KHR);
            GFXRECON_ASSERT(write.descriptorCount == structure->accelerationStructureCount);

            const auto* handles = meta_structure->pAccelerationStructures.GetPointer();
            for (uint32_t s = 0; s < write.descriptorCount; s++)
            {
                uint32_t array_index = write.dstArrayElement + s;
                auto     handle      = handles[s];

                binding_array_iter->second.descriptor_entries[array_index] = { .type         = write.descriptorType,
                                                                               .acceleration = { handle } };
            }
        }
        else
        {
            GFXRECON_LOG_DEBUG("call %llu vkUpdateDescriptorSets: unhandled descriptor type %s.",
                               call_info.index,
                               util::ToString<VkDescriptorType>(write.descriptorType).c_str());
        }
    }
}

void VulkanSpirvTrackModifier::Process_vkCreatePipelineLayout(
    const ApiCallInfo&                                        call_info,
    VkResult                                                  returnValue,
    format::HandleId                                          device,
    StructPointerDecoder<Decoded_VkPipelineLayoutCreateInfo>* pCreateInfo,
    StructPointerDecoder<Decoded_VkAllocationCallbacks>*      pAllocator,
    HandlePointerDecoder<VkPipelineLayout>*                   pPipelineLayout)
{
    if (IsModificationPass())
    {
        return;
    }

    format::HandleId handle      = *pPipelineLayout->GetPointer();
    const auto*      create_info = pCreateInfo->GetMetaStructPointer();

    GFXRECON_ASSERT(pipeline_layout_entries_.find(handle) == pipeline_layout_entries_.end());

    pipeline_layout_entries_[handle].handle            = handle;
    pipeline_layout_entries_[handle].creation_index    = call_info.index;
    pipeline_layout_entries_[handle].destruction_index = UINT64_MAX;
    pipeline_layout_entries_[handle].flags             = create_info->decoded_value->flags;

    uint32_t dynamic_start_index = 0;
    for (uint32_t i = 0; i < create_info->decoded_value->setLayoutCount; i++)
    {
        pipeline_layout_entries_[handle].setLayouts.push_back(create_info->pSetLayouts.GetPointer()[i]);

        // build dynamic binding map
        for (const auto& layout_binding_iter : set_layout_entries_[create_info->pSetLayouts.GetPointer()[i]].bindings)
        {
            const Binding& binding = layout_binding_iter.second;
            if (binding.type == VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC ||
                binding.type == VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC)
            {
                pipeline_layout_entries_[handle].dynamicBindingRef[i].emplace_back(
                    i, binding.binding, dynamic_start_index, binding.descriptorCount, binding.type);
                dynamic_start_index += binding.descriptorCount;
            }
        }
    }

    for (uint32_t i = 0; i < create_info->decoded_value->pushConstantRangeCount; i++)
    {
        VkPushConstantRange range = create_info->decoded_value->pPushConstantRanges[i];
        pipeline_layout_entries_[handle].pushConstantRanges.emplace_back(range);

#define PUSHRANGEINDICES(flagBits)                                                    \
    if ((range.stageFlags & flagBits) == flagBits)                                    \
    {                                                                                 \
        pipeline_layout_entries_[handle].rangeIndicesPerStage[flagBits].push_back(i); \
    }
        PUSHRANGEINDICES(VK_SHADER_STAGE_VERTEX_BIT);
        PUSHRANGEINDICES(VK_SHADER_STAGE_FRAGMENT_BIT);
        PUSHRANGEINDICES(VK_SHADER_STAGE_COMPUTE_BIT);
    }

    for (const auto& it : pipeline_layout_entries_[handle].rangeIndicesPerStage)
    {
        uint32_t offset = UINT32_MAX;
        uint32_t size   = 0;
        for (const auto& index : it.second)
        {
            const VkPushConstantRange& range = pipeline_layout_entries_[handle].pushConstantRanges[index];
            if (range.offset < offset)
            {
                offset = range.offset;
            }
            if (size < range.offset + range.size)
            {
                size = range.offset + range.size;
            }
        }
        pipeline_layout_entries_[handle].mergedRangePerStage[it.first] = { offset, size - offset };
    }
}

void VulkanSpirvTrackModifier::Process_vkDestroyPipelineLayout(
    const ApiCallInfo&                                   call_info,
    format::HandleId                                     device,
    format::HandleId                                     pipelineLayout,
    StructPointerDecoder<Decoded_VkAllocationCallbacks>* pAllocator)
{
    if (IsModificationPass())
    {
        return;
    }
    if (pipeline_layout_entries_.find(pipelineLayout) != pipeline_layout_entries_.end())
    {
        pipeline_layout_entries_[pipelineLayout].destruction_index = call_info.index;
    }
}

void VulkanSpirvTrackModifier::Process_vkCreateComputePipelines(
    const ApiCallInfo&                                         call_info,
    VkResult                                                   returnValue,
    format::HandleId                                           device,
    format::HandleId                                           pipelineCache,
    uint32_t                                                   createInfoCount,
    StructPointerDecoder<Decoded_VkComputePipelineCreateInfo>* pCreateInfos,
    StructPointerDecoder<Decoded_VkAllocationCallbacks>*       pAllocator,
    HandlePointerDecoder<VkPipeline>*                          pPipelines)
{
    if (IsModificationPass())
    {
        return;
    }

    const auto* create_info = pCreateInfos->GetMetaStructPointer();

    for (uint32_t idx = 0; idx < createInfoCount; idx++)
    {
        format::HandleId handle                     = pPipelines->GetPointer()[idx];
        pipeline_entries_[handle].handle            = handle;
        pipeline_entries_[handle].creation_index    = call_info.index;
        pipeline_entries_[handle].destruction_index = UINT64_MAX;
        pipeline_entries_[handle].layout            = create_info[idx].layout;

        Decoded_VkPipelineShaderStageCreateInfo* stage_info = create_info[idx].stage;

        ShaderStage stage;
        stage.pName                   = stage_info->pName.GetPointer();
        stage.module                  = stage_info->module;
        stage.stageFlagBit            = stage_info->decoded_value->stage;
        stage.specialization_dataSize = 0;

        const auto* spec_info = stage_info->pSpecializationInfo->GetMetaStructPointer();
        if (spec_info)
        {
            uint32_t entry_count = spec_info->decoded_value->mapEntryCount;

            const auto* entries = spec_info->pMapEntries->GetPointer();
            if (entries)
            {
                stage.specialization_dataSize = spec_info->decoded_value->dataSize;
                stage.specialization_constant = { spec_info->pData.GetPointer(),
                                                  spec_info->pData.GetPointer() + spec_info->decoded_value->dataSize };

                for (uint32_t i = 0; i < entry_count; i++)
                {
                    stage.specialization_offset_entries[entries[i].constantID] = entries[i].offset;
                }
            }
        }
        pipeline_entries_[handle].stages.push_back(stage);
    }
}

void VulkanSpirvTrackModifier::Process_vkCreateGraphicsPipelines(
    const ApiCallInfo&                                          call_info,
    VkResult                                                    returnValue,
    format::HandleId                                            device,
    format::HandleId                                            pipelineCache,
    uint32_t                                                    createInfoCount,
    StructPointerDecoder<Decoded_VkGraphicsPipelineCreateInfo>* pCreateInfos,
    StructPointerDecoder<Decoded_VkAllocationCallbacks>*        pAllocator,
    HandlePointerDecoder<VkPipeline>*                           pPipelines)
{
    if (IsModificationPass())
    {
        return;
    }

    const auto* create_info = pCreateInfos->GetMetaStructPointer();

    for (uint32_t idx = 0; idx < createInfoCount; idx++)
    {
        format::HandleId handle                     = pPipelines->GetPointer()[idx];
        pipeline_entries_[handle].handle            = handle;
        pipeline_entries_[handle].creation_index    = call_info.index;
        pipeline_entries_[handle].destruction_index = UINT64_MAX;
        pipeline_entries_[handle].layout            = create_info[idx].layout;

        const auto* stage_info = create_info[idx].pStages->GetMetaStructPointer();

        for (uint32_t i = 0; i < create_info[idx].decoded_value->stageCount; i++)
        {
            ShaderStage stage;
            stage.pName                   = stage_info[i].pName.GetPointer();
            stage.module                  = stage_info[i].module;
            stage.stageFlagBit            = stage_info[i].decoded_value->stage;
            stage.specialization_dataSize = 0;

            const auto* spec_info = stage_info[i].pSpecializationInfo->GetMetaStructPointer();
            if (spec_info)
            {
                uint32_t    entry_count = spec_info->decoded_value->mapEntryCount;
                const auto* entries     = spec_info->pMapEntries->GetPointer();

                if (entries)
                {
                    stage.specialization_dataSize = spec_info->decoded_value->dataSize;
                    stage.specialization_constant = { spec_info->pData.GetPointer(),
                                                      spec_info->pData.GetPointer() +
                                                          spec_info->decoded_value->dataSize };

                    for (uint32_t j = 0; j < entry_count; j++)
                    {
                        stage.specialization_offset_entries[entries[i].constantID] = entries[i].offset;
                    }
                }
            }
            pipeline_entries_[handle].stages.push_back(stage);
        }
    }
}

void VulkanSpirvTrackModifier::Process_vkDestroyPipeline(
    const ApiCallInfo&                                   call_info,
    format::HandleId                                     device,
    format::HandleId                                     pipeline,
    StructPointerDecoder<Decoded_VkAllocationCallbacks>* pAllocator)
{
    if (IsModificationPass())
    {
        return;
    }
    if (pipeline_entries_.find(pipeline) != pipeline_entries_.end())
    {
        pipeline_entries_[pipeline].destruction_index = call_info.index;
    }
}

void VulkanSpirvTrackModifier::Process_vkAllocateCommandBuffers(
    const ApiCallInfo&                                         call_info,
    VkResult                                                   returnValue,
    format::HandleId                                           device,
    StructPointerDecoder<Decoded_VkCommandBufferAllocateInfo>* pAllocateInfo,
    HandlePointerDecoder<VkCommandBuffer>*                     pCommandBuffers)
{
    if (IsModificationPass())
    {
        return;
    }

    const VkCommandBufferAllocateInfo* alloc_info = pAllocateInfo->GetPointer();

    for (uint32_t i = 0; i < alloc_info->commandBufferCount; i++)
    {
        format::HandleId handle = pCommandBuffers->GetPointer()[i];
        GFXRECON_ASSERT(command_buffer_entries_.find(handle) == command_buffer_entries_.end());

        command_buffer_entries_[handle].handle            = handle;
        command_buffer_entries_[handle].creation_index    = call_info.index;
        command_buffer_entries_[handle].destruction_index = UINT64_MAX;
        command_buffer_entries_[handle].state             = CommandBufferLifeCycle::Inital;
    }
}

void VulkanSpirvTrackModifier::Process_vkFreeCommandBuffers(const ApiCallInfo&                     call_info,
                                                            format::HandleId                       device,
                                                            format::HandleId                       commandPool,
                                                            uint32_t                               commandBufferCount,
                                                            HandlePointerDecoder<VkCommandBuffer>* pCommandBuffers)
{
    if (IsModificationPass())
    {
        return;
    }

    for (uint32_t i = 0; i < commandBufferCount; i++)
    {
        format::HandleId handle = pCommandBuffers->GetPointer()[i];
        command_buffer_entries_.erase(handle);

        command_buffer_recording.erase(handle);
        command_buffer_submit_recordings.erase(handle);
        command_buffer_state.erase(handle);
    }
}

void VulkanSpirvTrackModifier::Process_vkResetCommandBuffer(const ApiCallInfo&        call_info,
                                                            VkResult                  returnValue,
                                                            format::HandleId          commandBuffer,
                                                            VkCommandBufferResetFlags flags)
{
    if (IsModificationPass())
    {
        return;
    }

    if (command_buffer_entries_.find(commandBuffer) == command_buffer_entries_.end())
    {
        return;
    }

    if (command_buffer_recording.find(commandBuffer) != command_buffer_recording.end() &&
        command_buffer_entries_[commandBuffer].state == CommandBufferLifeCycle::Pending)
    {
        GFXRECON_LOG_DEBUG("command buffer %llu is in pending state. Return from reset.", commandBuffer);
        return;
    }

    command_buffer_entries_[commandBuffer].state = CommandBufferLifeCycle::Inital;

    command_buffer_recording.erase(commandBuffer);
    command_buffer_submit_recordings.erase(commandBuffer);
    command_buffer_state.erase(commandBuffer);
}

void VulkanSpirvTrackModifier::Process_vkBeginCommandBuffer(
    const ApiCallInfo&                                      call_info,
    VkResult                                                returnValue,
    format::HandleId                                        commandBuffer,
    StructPointerDecoder<Decoded_VkCommandBufferBeginInfo>* pBeginInfo)
{
    if (IsModificationPass())
    {
        return;
    }

    if (command_buffer_entries_.find(commandBuffer) == command_buffer_entries_.end())
    {
        return;
    }

    if (command_buffer_recording.find(commandBuffer) != command_buffer_recording.end() &&
        command_buffer_entries_[commandBuffer].state == CommandBufferLifeCycle::Pending)
    {
        GFXRECON_LOG_INFO("command buffer %llu is in pending state. Return from begin.", commandBuffer);
        return;
    }

    if (command_buffer_recording.find(commandBuffer) != command_buffer_recording.end() &&
        (command_buffer_entries_[commandBuffer].state == CommandBufferLifeCycle::Recording ||
         command_buffer_entries_[commandBuffer].state == CommandBufferLifeCycle::Executable))
    {
        GFXRECON_LOG_INFO("command buffer %llu has been in recording/executable state and will be reset now.",
                          commandBuffer);
    }

    command_buffer_entries_[commandBuffer].state = CommandBufferLifeCycle::Recording;

    command_buffer_recording.erase(commandBuffer);
    command_buffer_submit_recordings.erase(commandBuffer);
    command_buffer_state.erase(commandBuffer);

    command_buffer_recording[commandBuffer].command_buffer = commandBuffer;
    command_buffer_recording[commandBuffer].bind_point     = VK_PIPELINE_BIND_POINT_MAX_ENUM;

    command_buffer_state[commandBuffer].command_buffer = commandBuffer;
    command_buffer_state[commandBuffer].push_constant.resize(256); // MAX_PUSHCONSTANT_SIZE
}

void VulkanSpirvTrackModifier::Process_vkEndCommandBuffer(const ApiCallInfo& call_info,
                                                          VkResult           returnValue,
                                                          format::HandleId   commandBuffer)
{
    if (IsModificationPass())
    {
        return;
    }
    if (command_buffer_entries_.find(commandBuffer) == command_buffer_entries_.end())
    {
        return;
    }
    command_buffer_entries_.at(commandBuffer).state = CommandBufferLifeCycle::Executable;
}

void VulkanSpirvTrackModifier::Process_vkGetBufferDeviceAddress(
    const ApiCallInfo&                                       call_info,
    VkDeviceAddress                                          returnValue,
    format::HandleId                                         device,
    StructPointerDecoder<Decoded_VkBufferDeviceAddressInfo>* pInfo)
{
    if (IsModificationPass())
    {
        return;
    }
    const auto& buffer_id                     = pInfo->GetMetaStructPointer()->buffer;
    buffer_device_addresses_[returnValue]     = buffer_id;
    buffer_entries_[buffer_id].device_address = returnValue;
}

void VulkanSpirvTrackModifier::Process_vkGetBufferDeviceAddressKHR(
    const ApiCallInfo&                                       call_info,
    VkDeviceAddress                                          returnValue,
    format::HandleId                                         device,
    StructPointerDecoder<Decoded_VkBufferDeviceAddressInfo>* pInfo)
{
    if (IsModificationPass())
    {
        return;
    }
    const auto& buffer_id                     = pInfo->GetMetaStructPointer()->buffer;
    buffer_device_addresses_[returnValue]     = buffer_id;
    buffer_entries_[buffer_id].device_address = returnValue;
}

void VulkanSpirvTrackModifier::Process_vkGetBufferDeviceAddressEXT(
    const ApiCallInfo&                                       call_info,
    VkDeviceAddress                                          returnValue,
    format::HandleId                                         device,
    StructPointerDecoder<Decoded_VkBufferDeviceAddressInfo>* pInfo)
{
    if (IsModificationPass())
    {
        return;
    }
    const auto& buffer_id                     = pInfo->GetMetaStructPointer()->buffer;
    buffer_device_addresses_[returnValue]     = buffer_id;
    buffer_entries_[buffer_id].device_address = returnValue;
}

void VulkanSpirvTrackModifier::Process_vkGetAccelerationStructureDeviceAddressKHR(
    const ApiCallInfo&                                                         call_info,
    VkDeviceAddress                                                            returnValue,
    format::HandleId                                                           device,
    StructPointerDecoder<Decoded_VkAccelerationStructureDeviceAddressInfoKHR>* pInfo)
{
    if (IsModificationPass())
    {
        return;
    }

    const auto& as_id = pInfo->GetMetaStructPointer()->accelerationStructure;
    acceleration_structure_device_addresses_[returnValue].insert(as_id);
    if (acceleration_structure_entries_.find(as_id) != acceleration_structure_entries_.end())
    {
        acceleration_structure_entries_[as_id].device_address = returnValue;
        if (buffer_device_addresses_.find(returnValue) != buffer_device_addresses_.end())
        {
            buffer_device_addresses_.erase(returnValue);
        }
    }
}

void VulkanSpirvTrackModifier::Process_vkCmdBindPipeline(const ApiCallInfo&  call_info,
                                                         format::HandleId    commandBuffer,
                                                         VkPipelineBindPoint pipelineBindPoint,
                                                         format::HandleId    pipeline)
{
    if (IsModificationPass())
    {
        return;
    }

    // As long as in Recording state, the command_buffer_recording contains the commandBuffer value.
    if (command_buffer_entries_.find(commandBuffer) == command_buffer_entries_.end() ||
        command_buffer_entries_[commandBuffer].state != CommandBufferLifeCycle::Recording)
    {
        return;
    }

    command_buffer_recording[commandBuffer].pipelines[pipelineBindPoint] = pipeline;
}

void VulkanSpirvTrackModifier::Process_vkCmdBindDescriptorSets(const ApiCallInfo&  call_info,
                                                               format::HandleId    commandBuffer,
                                                               VkPipelineBindPoint pipelineBindPoint,
                                                               format::HandleId    layout,
                                                               uint32_t            firstSet,
                                                               uint32_t            descriptorSetCount,
                                                               HandlePointerDecoder<VkDescriptorSet>* pDescriptorSets,
                                                               uint32_t                  dynamicOffsetCount,
                                                               PointerDecoder<uint32_t>* pDynamicOffsets)
{
    if (IsModificationPass())
    {
        return;
    }

    if (command_buffer_entries_.find(commandBuffer) == command_buffer_entries_.end() ||
        command_buffer_entries_[commandBuffer].state != CommandBufferLifeCycle::Recording)
    {
        return;
    }

    DescriptorStateCommand command;
    command.type       = DescriptorStateCommand::Type::BindDescriptorSets;
    command.bind_point = pipelineBindPoint;

    for (uint32_t i = 0; i < descriptorSetCount; i++)
    {
        format::HandleId descriptor_set = pDescriptorSets->GetPointer()[i];
        command.descriptor_sets.push_back({ firstSet + i, descriptor_set, layout });
    }

    if (dynamicOffsetCount > 0 && pDynamicOffsets != nullptr)
    {
        uint32_t   consumed_index = 0;
        uint32_t   consumed_count = 0;
        uint32_t   remained_count = dynamicOffsetCount;
        const auto layout_iter    = pipeline_layout_entries_.find(layout);
        for (uint32_t i = 0; i < descriptorSetCount; i++)
        {
            const auto ref_iter = layout_iter->second.dynamicBindingRef.find(firstSet + i);
            if (ref_iter != layout_iter->second.dynamicBindingRef.end())
            {
                if (remained_count == 0)
                {
                    GFXRECON_LOG_INFO("call %llu vkCmdBindDescriptorSets: Dynamic offset mismatch on set[%u].",
                                      call_info.index,
                                      firstSet + i);
                    break;
                }

                DynamicBindingOffsetMap elem;
                elem.set = firstSet + i;
                for (const DynamicBindingRef& ref : ref_iter->second)
                {
                    if (remained_count >= ref.elementCount)
                    {
                        consumed_count = ref.elementCount;
                        remained_count -= ref.elementCount;
                    }
                    else if (remained_count == 0)
                    {
                        GFXRECON_LOG_INFO(
                            "call %llu vkCmdBindDescriptorSets: Dynamic offset mismatch on set[%u] binding[%u].",
                            call_info.index,
                            firstSet + i,
                            ref.binding);
                        break;
                    }
                    else
                    {
                        consumed_count = remained_count;
                        remained_count = 0;
                        GFXRECON_LOG_INFO(
                            "call %llu vkCmdBindDescriptorSets: Dynamic offset mismatch on set[%u] binding[%u].",
                            call_info.index,
                            firstSet + i,
                            ref.binding);
                    }
                    elem.binding_offsets[ref.binding] = { pDynamicOffsets->GetPointer() + consumed_index,
                                                          pDynamicOffsets->GetPointer() + consumed_index +
                                                              consumed_count };
                    consumed_index += consumed_count;
                }
                command.dynamic_offsets[elem.set] = std::move(elem);
            }
        }
    }

    if (!command.descriptor_sets.empty())
    {
        command_buffer_recording[commandBuffer].descriptor_state_commands.push_back(std::move(command));
    }
}

void VulkanSpirvTrackModifier::Process_vkCmdBindDescriptorBuffersEXT(
    const ApiCallInfo&                                              call_info,
    format::HandleId                                                commandBuffer,
    uint32_t                                                        bufferCount,
    StructPointerDecoder<Decoded_VkDescriptorBufferBindingInfoEXT>* pBindingInfos)
{
    if (IsModificationPass())
    {
        return;
    }

    if (command_buffer_entries_.find(commandBuffer) == command_buffer_entries_.end() ||
        command_buffer_entries_[commandBuffer].state != CommandBufferLifeCycle::Recording)
    {
        return;
    }

    DescriptorStateCommand command;
    command.type = DescriptorStateCommand::Type::BindDescriptorBuffers;

    const auto* binding_info      = pBindingInfos->GetPointer();
    const auto* meta_binding_info = pBindingInfos->GetMetaStructPointer();
    for (uint32_t i = 0; i < bufferCount; i++)
    {
        format::HandleId    handle       = 0;
        VkDeviceAddress     base_address = 0;
        VkBufferUsageFlags2 usage2       = binding_info[i].usage;
        bool                resolved     = false;
        auto                iter         = findEntryFromBufferDeviceAddress(binding_info[i].address);

        const auto* usage2_struct_info =
            GetPNextMetaStruct<Decoded_VkBufferUsageFlags2CreateInfo>(meta_binding_info[i].pNext);
        if (usage2_struct_info != nullptr && usage2_struct_info->decoded_value != nullptr)
        {
            usage2 = usage2_struct_info->decoded_value->usage;
        }

        if (iter != buffer_device_addresses_.end())
        {
            base_address = iter->first;
            handle       = iter->second;

            const auto* ext_struct_info =
                GetPNextMetaStruct<Decoded_VkDescriptorBufferBindingPushDescriptorBufferHandleEXT>(
                    meta_binding_info[i].pNext);
            if (ext_struct_info == nullptr || ext_struct_info->buffer == handle)
            {
                resolved = true;
            }
            else
            {
                GFXRECON_LOG_INFO(
                    "call %llu vkCmdBindDescriptorBuffersEXT: bound descriptor buffer handle %llu does not match "
                    "VkDescriptorBufferBindingPushDescriptorBufferHandleEXT buffer %llu.",
                    call_info.index,
                    handle,
                    ext_struct_info->buffer);
            }
        }
        else
        {
            GFXRECON_LOG_INFO("call %llu vkCmdBindDescriptorBuffersEXT: the device address 0x%lx of bound "
                              "descriptor buffer does not existed.",
                              call_info.index,
                              binding_info[i].address);
        }
        command.descriptor_buffers.push_back(
            { handle, base_address, binding_info[i].address, binding_info[i].usage, usage2, resolved });
    }

    command_buffer_recording[commandBuffer].descriptor_state_commands.push_back(std::move(command));
}

void VulkanSpirvTrackModifier::Process_vkCmdSetDescriptorBufferOffsetsEXT(const ApiCallInfo&        call_info,
                                                                          format::HandleId          commandBuffer,
                                                                          VkPipelineBindPoint       pipelineBindPoint,
                                                                          format::HandleId          layout,
                                                                          uint32_t                  firstSet,
                                                                          uint32_t                  setCount,
                                                                          PointerDecoder<uint32_t>* pBufferIndices,
                                                                          PointerDecoder<VkDeviceSize>* pOffsets)
{
    if (IsModificationPass())
    {
        return;
    }

    if (command_buffer_entries_.find(commandBuffer) == command_buffer_entries_.end() ||
        command_buffer_entries_[commandBuffer].state != CommandBufferLifeCycle::Recording)
    {
        return;
    }

    DescriptorStateCommand command;
    command.type       = DescriptorStateCommand::Type::SetDescriptorBufferOffsets;
    command.bind_point = pipelineBindPoint;

    for (uint32_t i = 0; i < setCount; i++)
    {
        uint32_t     buffer_index = pBufferIndices->GetPointer()[i];
        VkDeviceSize offset       = pOffsets->GetPointer()[i];

        command.descriptor_buffer_offsets.emplace_back(firstSet + i, buffer_index, offset, layout);
    }

    if (!command.descriptor_buffer_offsets.empty())
    {
        command_buffer_recording[commandBuffer].descriptor_state_commands.push_back(std::move(command));
    }
}

void VulkanSpirvTrackModifier::Process_vkCmdPushConstants(const ApiCallInfo&       call_info,
                                                          format::HandleId         commandBuffer,
                                                          format::HandleId         layout,
                                                          VkShaderStageFlags       stageFlags,
                                                          uint32_t                 offset,
                                                          uint32_t                 size,
                                                          PointerDecoder<uint8_t>* pValues)
{
    if (IsModificationPass())
    {
        return;
    }

    if (command_buffer_entries_.find(commandBuffer) == command_buffer_entries_.end() ||
        command_buffer_entries_[commandBuffer].state != CommandBufferLifeCycle::Recording)
    {
        return;
    }

    std::vector<uint8_t> values(size);
    std::memcpy(values.data(), pValues->GetPointer(), size);

    command_buffer_recording[commandBuffer].push_constants.push_back(
        { offset, size, stageFlags, layout, std::move(values) });
}

void VulkanSpirvTrackModifier::Process_vkCmdFillBuffer(const ApiCallInfo& call_info,
                                                       format::HandleId   commandBuffer,
                                                       format::HandleId   dstBuffer,
                                                       VkDeviceSize       dstOffset,
                                                       VkDeviceSize       size,
                                                       uint32_t           data)
{
    if (IsModificationPass())
    {
        return;
    }

    if (command_buffer_entries_.find(commandBuffer) == command_buffer_entries_.end() ||
        command_buffer_entries_[commandBuffer].state != CommandBufferLifeCycle::Recording)
    {
        return;
    }

    uint8_t* data_ptr = reinterpret_cast<uint8_t*>(&data);
    command_buffer_recording[commandBuffer].buffer_write_list.emplace_back(SourceType::Fill, dstBuffer);
    command_buffer_recording[commandBuffer].buffer_write_list.back().srcPointer = { data_ptr,
                                                                                    data_ptr + sizeof(uint32_t) };
    command_buffer_recording[commandBuffer].buffer_write_list.back().regions.emplace_back(0, dstOffset, size);

    command_buffer_recording[commandBuffer].in_operation = true;
}

void VulkanSpirvTrackModifier::Process_vkCmdUpdateBuffer(const ApiCallInfo&       call_info,
                                                         format::HandleId         commandBuffer,
                                                         format::HandleId         dstBuffer,
                                                         VkDeviceSize             dstOffset,
                                                         VkDeviceSize             dataSize,
                                                         PointerDecoder<uint8_t>* pData)
{
    if (IsModificationPass())
    {
        return;
    }

    if (command_buffer_entries_.find(commandBuffer) == command_buffer_entries_.end() ||
        command_buffer_entries_[commandBuffer].state != CommandBufferLifeCycle::Recording)
    {
        return;
    }

    command_buffer_recording[commandBuffer].buffer_write_list.emplace_back(SourceType::Update, dstBuffer);

    command_buffer_recording[commandBuffer].buffer_write_list.back().srcPointer = { pData->GetPointer(),
                                                                                    pData->GetPointer() + dataSize };

    command_buffer_recording[commandBuffer].buffer_write_list.back().regions.emplace_back(0, dstOffset, dataSize);

    command_buffer_recording[commandBuffer].in_operation = true;
}

void VulkanSpirvTrackModifier::Process_vkCmdCopyBuffer(const ApiCallInfo&                          call_info,
                                                       format::HandleId                            commandBuffer,
                                                       format::HandleId                            srcBuffer,
                                                       format::HandleId                            dstBuffer,
                                                       uint32_t                                    regionCount,
                                                       StructPointerDecoder<Decoded_VkBufferCopy>* pRegions)
{
    if (IsModificationPass())
    {
        return;
    }

    if (command_buffer_entries_.find(commandBuffer) == command_buffer_entries_.end() ||
        command_buffer_entries_[commandBuffer].state != CommandBufferLifeCycle::Recording)
    {
        return;
    }

    const VkBufferCopy* regions = pRegions->GetPointer();

    command_buffer_recording[commandBuffer].buffer_write_list.emplace_back(
        SourceType::CopyBuffer, dstBuffer, srcBuffer);

    for (uint32_t i = 0; i < regionCount; i++)
    {
        command_buffer_recording[commandBuffer].buffer_write_list.back().regions.emplace_back(regions[i]);
    }

    command_buffer_recording[commandBuffer].in_operation = true;
}

void VulkanSpirvTrackModifier::Process_vkCmdCopyBuffer2(
    const ApiCallInfo&                               call_info,
    format::HandleId                                 commandBuffer,
    StructPointerDecoder<Decoded_VkCopyBufferInfo2>* pCopyBufferInfo)
{
    if (IsModificationPass())
    {
        return;
    }

    if (command_buffer_entries_.find(commandBuffer) == command_buffer_entries_.end() ||
        command_buffer_entries_[commandBuffer].state != CommandBufferLifeCycle::Recording)
    {
        return;
    }

    const auto* meta_copy_info = pCopyBufferInfo->GetMetaStructPointer();
    const auto* copy2          = meta_copy_info->pRegions->GetPointer();

    command_buffer_recording[commandBuffer].buffer_write_list.emplace_back(
        SourceType::CopyBuffer, meta_copy_info->dstBuffer, meta_copy_info->srcBuffer);

    for (uint32_t i = 0; i < meta_copy_info->decoded_value->regionCount; i++)
    {
        command_buffer_recording[commandBuffer].buffer_write_list.back().regions.emplace_back(
            copy2[i].srcOffset, copy2[i].dstOffset, copy2[i].size);
    }

    command_buffer_recording[commandBuffer].in_operation = true;
}

void VulkanSpirvTrackModifier::Process_vkCmdCopyBuffer2KHR(
    const ApiCallInfo&                               call_info,
    format::HandleId                                 commandBuffer,
    StructPointerDecoder<Decoded_VkCopyBufferInfo2>* pCopyBufferInfo)
{
    if (IsModificationPass())
    {
        return;
    }

    if (command_buffer_entries_.find(commandBuffer) == command_buffer_entries_.end() ||
        command_buffer_entries_[commandBuffer].state != CommandBufferLifeCycle::Recording)
    {
        return;
    }

    const auto* meta_copy_info = pCopyBufferInfo->GetMetaStructPointer();
    const auto* copy2          = meta_copy_info->pRegions->GetPointer();

    command_buffer_recording[commandBuffer].buffer_write_list.emplace_back(
        SourceType::CopyBuffer, meta_copy_info->dstBuffer, meta_copy_info->srcBuffer);

    for (uint32_t i = 0; i < meta_copy_info->decoded_value->regionCount; i++)
    {
        command_buffer_recording[commandBuffer].buffer_write_list.back().regions.emplace_back(
            copy2[i].srcOffset, copy2[i].dstOffset, copy2[i].size);
    }

    command_buffer_recording[commandBuffer].in_operation = true;
}

void VulkanSpirvTrackModifier::Process_vkCmdBuildAccelerationStructuresKHR(
    const ApiCallInfo&                                                         call_info,
    format::HandleId                                                           commandBuffer,
    uint32_t                                                                   infoCount,
    StructPointerDecoder<Decoded_VkAccelerationStructureBuildGeometryInfoKHR>* pInfos,
    StructPointerDecoder<Decoded_VkAccelerationStructureBuildRangeInfoKHR*>*   ppBuildRangeInfos)
{
    if (IsModificationPass())
    {
        return;
    }

    if (command_buffer_entries_.find(commandBuffer) == command_buffer_entries_.end() ||
        command_buffer_entries_[commandBuffer].state != CommandBufferLifeCycle::Recording)
    {
        return;
    }
    CommandBufferRecording& recording = command_buffer_recording.at(commandBuffer);

    for (uint32_t info_index = 0; info_index < infoCount; info_index++)
    {
        const auto& build_range_info         = ppBuildRangeInfos->GetPointer()[info_index];
        const auto& build_geometry_meta_info = pInfos->GetMetaStructPointer()[info_index];
        const auto& build_geometry_info      = pInfos->GetPointer()[info_index];

        format::HandleId dst_as = build_geometry_meta_info.dstAccelerationStructure;
        format::HandleId src_as = build_geometry_meta_info.srcAccelerationStructure;

        const auto* geometries_meta = build_geometry_meta_info.pGeometries->GetMetaStructPointer();
        const auto* geometries      = build_geometry_meta_info.pGeometries->GetPointer();

        // TODO: support ppGeometries
        GFXRECON_ASSERT(geometries_meta != nullptr && geometries != nullptr);

        for (uint32_t geometry_index = 0; geometry_index < build_geometry_info.geometryCount; geometry_index++)
        {
            const Decoded_VkAccelerationStructureGeometryKHR& geometry_meta = geometries_meta[geometry_index];
            const VkAccelerationStructureGeometryKHR&         geometry      = geometries[geometry_index];

            recording.build_acceleration_structure[dst_as].primitive_counts.push_back(
                build_range_info[geometry_index].primitiveCount);
            recording.build_acceleration_structure[dst_as].primitive_offsets.push_back(
                build_range_info[geometry_index].primitiveOffset);

            switch (geometry.geometryType)
            {
                case VK_GEOMETRY_TYPE_TRIANGLES_KHR:
                {
                    GFXRECON_ASSERT(build_geometry_info.type == VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR);
                    break;
                }
                case VK_GEOMETRY_TYPE_AABBS_KHR:
                {
                    GFXRECON_ASSERT(build_geometry_info.type == VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR);
                    break;
                }
                case VK_GEOMETRY_TYPE_INSTANCES_KHR:
                {
                    GFXRECON_ASSERT(build_geometry_info.type == VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR);
                    VkDeviceAddress address = geometry.geometry.instances.data.deviceAddress;
                    auto            entry   = std::find_if(
                        buffer_device_addresses_.begin(), buffer_device_addresses_.end(), [address, this](auto& entry) {
                            auto it = buffer_entries_.find(entry.second);
                            if (it == buffer_entries_.end())
                            {
                                return false;
                            }
                            const auto& buffer_info = it->second;
                            return (address >= entry.first) && (address < entry.first + buffer_info.size);
                        });
                    if (entry != buffer_device_addresses_.end())
                    {
                        recording.build_acceleration_structure[dst_as].instance_buffers.push_back(
                            std::pair<format::HandleId, uint64_t>{ entry->second, address - entry->first });
                    }
                    if (geometry.geometry.instances.arrayOfPointers == false)
                    {
                        const BufferInfo& buffer_info = buffer_entries_.at(buffer_device_addresses_[address]);
                        // VkAccelerationStructureInstanceKHR
                    }
                    break;
                }
                default:
                {
                    GFXRECON_LOG_ERROR("Unexpected geometry type in CmdBuildAccelerationStructures.");
                    break;
                }
            }
        }
    }

    command_buffer_recording[commandBuffer].in_operation = true;
}

void VulkanSpirvTrackModifier::Process_vkCmdDispatch(const ApiCallInfo& call_info,
                                                     format::HandleId   commandBuffer,
                                                     uint32_t           groupCountX,
                                                     uint32_t           groupCountY,
                                                     uint32_t           groupCountZ)
{
    if (IsModificationPass())
    {
        return;
    }

    if (command_buffer_entries_.find(commandBuffer) == command_buffer_entries_.end() ||
        command_buffer_entries_[commandBuffer].state != CommandBufferLifeCycle::Recording)
    {
        return;
    }
    command_buffer_recording[commandBuffer].bind_point = VK_PIPELINE_BIND_POINT_COMPUTE;
    command_buffer_submit_recordings[commandBuffer].emplace_back(command_buffer_recording[commandBuffer]);
    resetRecording(commandBuffer);
}

void VulkanSpirvTrackModifier::Process_vkCmdDispatchIndirect(const ApiCallInfo& call_info,
                                                             format::HandleId   commandBuffer,
                                                             format::HandleId   buffer,
                                                             VkDeviceSize       offset)
{
    if (IsModificationPass())
    {
        return;
    }

    if (command_buffer_entries_.find(commandBuffer) == command_buffer_entries_.end() ||
        command_buffer_entries_[commandBuffer].state != CommandBufferLifeCycle::Recording)
    {
        return;
    }

    command_buffer_recording[commandBuffer].bind_point = VK_PIPELINE_BIND_POINT_COMPUTE;
    command_buffer_submit_recordings[commandBuffer].emplace_back(command_buffer_recording[commandBuffer]);
    resetRecording(commandBuffer);
}

void VulkanSpirvTrackModifier::Process_vkCmdDraw(const ApiCallInfo& call_info,
                                                 format::HandleId   commandBuffer,
                                                 uint32_t           vertexCount,
                                                 uint32_t           instanceCount,
                                                 uint32_t           firstVertex,
                                                 uint32_t           firstInstance)
{
    if (IsModificationPass())
    {
        return;
    }

    if (command_buffer_entries_.find(commandBuffer) == command_buffer_entries_.end() ||
        command_buffer_entries_[commandBuffer].state != CommandBufferLifeCycle::Recording)
    {
        return;
    }

    command_buffer_recording[commandBuffer].bind_point = VK_PIPELINE_BIND_POINT_GRAPHICS;
    command_buffer_submit_recordings[commandBuffer].emplace_back(command_buffer_recording[commandBuffer]);
    resetRecording(commandBuffer);
}

void VulkanSpirvTrackModifier::Process_vkCmdDrawIndexed(const ApiCallInfo& call_info,
                                                        format::HandleId   commandBuffer,
                                                        uint32_t           indexCount,
                                                        uint32_t           instanceCount,
                                                        uint32_t           firstIndex,
                                                        int32_t            vertexOffset,
                                                        uint32_t           firstInstance)
{
    if (IsModificationPass())
    {
        return;
    }

    if (command_buffer_entries_.find(commandBuffer) == command_buffer_entries_.end() ||
        command_buffer_entries_[commandBuffer].state != CommandBufferLifeCycle::Recording)
    {
        return;
    }

    command_buffer_recording[commandBuffer].bind_point = VK_PIPELINE_BIND_POINT_GRAPHICS;
    command_buffer_submit_recordings[commandBuffer].emplace_back(command_buffer_recording[commandBuffer]);
    resetRecording(commandBuffer);
}

void VulkanSpirvTrackModifier::Process_vkCmdDrawIndirect(const ApiCallInfo& call_info,
                                                         format::HandleId   commandBuffer,
                                                         format::HandleId   buffer,
                                                         VkDeviceSize       offset,
                                                         uint32_t           drawCount,
                                                         uint32_t           stride)
{
    if (IsModificationPass())
    {
        return;
    }

    if (command_buffer_entries_.find(commandBuffer) == command_buffer_entries_.end() ||
        command_buffer_entries_[commandBuffer].state != CommandBufferLifeCycle::Recording)
    {
        return;
    }

    command_buffer_recording[commandBuffer].bind_point = VK_PIPELINE_BIND_POINT_GRAPHICS;
    command_buffer_submit_recordings[commandBuffer].emplace_back(command_buffer_recording[commandBuffer]);
    resetRecording(commandBuffer);
}

void VulkanSpirvTrackModifier::Process_vkCmdDrawIndexedIndirect(const ApiCallInfo& call_info,
                                                                format::HandleId   commandBuffer,
                                                                format::HandleId   buffer,
                                                                VkDeviceSize       offset,
                                                                uint32_t           drawCount,
                                                                uint32_t           stride)
{
    if (IsModificationPass())
    {
        return;
    }

    if (command_buffer_entries_.find(commandBuffer) == command_buffer_entries_.end() ||
        command_buffer_entries_[commandBuffer].state != CommandBufferLifeCycle::Recording)
    {
        return;
    }

    command_buffer_recording[commandBuffer].bind_point = VK_PIPELINE_BIND_POINT_GRAPHICS;
    command_buffer_submit_recordings[commandBuffer].emplace_back(command_buffer_recording[commandBuffer]);
    resetRecording(commandBuffer);
}

void VulkanSpirvTrackModifier::process_SubmitInfo(uint64_t                             submitIndex,
                                                  const std::vector<format::HandleId>& commandBuffers,
                                                  const std::vector<format::HandleId>& waitSems,
                                                  const std::vector<format::HandleId>& signalSems,
                                                  const std::vector<uint64_t>&         waitValues,
                                                  const std::vector<uint64_t>&         signalValues)
{
    bool allWaitSatisfied = true;
    // whether all the wait semaphores are signaled. Set all the semaphores to null by default.
    for (size_t i = 0; i < waitSems.size(); i++)
    {
        if (semaphore_state.find(waitSems[i]) == semaphore_state.end())
        {
            continue;
        }

        SemaphoreState& wait_state = semaphore_state.at(waitSems[i]);
        if (wait_state.type == VK_SEMAPHORE_TYPE_TIMELINE)
        {
            GFXRECON_ASSERT(waitValues.size() == waitSems.size());
            GFXRECON_ASSERT(std::holds_alternative<TimelineSemaphoreState>(wait_state.state));

            uint64_t required = waitValues[i];
            auto&    timeline = std::get<TimelineSemaphoreState>(wait_state.state);
            if (timeline.currentValue < required)
            {
                allWaitSatisfied = false;
                timeline.waitingSubmits.emplace_back(submitIndex, required);
            }
        }
        else
        {
            GFXRECON_ASSERT(std::holds_alternative<BinarySemaphoreState>(wait_state.state));

            auto& binary = std::get<BinarySemaphoreState>(wait_state.state);
            if (!binary.signaled)
            {
                allWaitSatisfied = false;
                binary.waitingSubmits.push_back(submitIndex);
            }
        }
    }

    SubmitInfo info;
    info.submit_index      = submitIndex;
    info.command_buffers   = commandBuffers;
    info.wait_semaphores   = waitSems;
    info.signal_semaphores = signalSems;
    info.wait_values       = waitValues;
    info.signal_values     = signalValues;
    info.ready             = allWaitSatisfied;

    submit_entries_[submitIndex] = info;
    if (allWaitSatisfied)
    {
        ready_submits.push_back(submitIndex);
    }
}

void VulkanSpirvTrackModifier::signalSemaphoresFrom(uint64_t submitIndex)
{
    SubmitInfo& signaled_submit_info = submit_entries_.at(submitIndex);
    GFXRECON_ASSERT(signaled_submit_info.submitted);

    // the submit has been executed, signal the depending semaphores
    for (size_t i = 0; i < signaled_submit_info.signal_semaphores.size(); i++)
    {
        SemaphoreState& signaled_state = semaphore_state.at(signaled_submit_info.signal_semaphores[i]);

        if (signaled_state.type == VK_SEMAPHORE_TYPE_TIMELINE)
        {
            auto& timeline = std::get<TimelineSemaphoreState>(signaled_state.state);
            // update timeline semaphore's value with the signaled value
            uint64_t signaledVal = signaled_submit_info.signal_values[i];
            if (signaledVal > timeline.currentValue)
            {
                timeline.currentValue = signaledVal;
            }

            std::deque<std::pair<uint64_t, uint64_t>> remained_waiting;
            // traversal the submits waiting on the signaled sem
            while (!timeline.waitingSubmits.empty())
            {
                auto [dependent_submit, requiredVal] = timeline.waitingSubmits.front();
                timeline.waitingSubmits.pop_front();
                if (timeline.currentValue > requiredVal)
                {
                    SubmitInfo& dependent_submit_info = submit_entries_[dependent_submit];
                    bool        allReady              = true;
                    // whether all the wait_sem are signaled
                    for (size_t i = 0; i < dependent_submit_info.wait_semaphores.size(); i++)
                    {
                        // dependent submit's waitSem also timeline
                        auto handle = dependent_submit_info.wait_semaphores[i];
                        if (std::get<TimelineSemaphoreState>(semaphore_state[handle].state).currentValue <
                            dependent_submit_info.wait_values[i])
                        {
                            allReady = false;
                            break;
                        }
                    }
                    if (allReady && !dependent_submit_info.ready)
                    {
                        dependent_submit_info.ready = true;
                        ready_submits.push_back(dependent_submit);
                    }
                }
                else
                {
                    remained_waiting.push_back({ dependent_submit, requiredVal });
                }
            }
            timeline.waitingSubmits = std::move(remained_waiting);
        }
        else
        {
            auto& binary            = std::get<BinarySemaphoreState>(signaled_state.state);
            binary.signaled         = true;
            binary.signaledBySubmit = submitIndex;
            // traversal the submits waiting on the signaled sem
            while (!binary.waitingSubmits.empty())
            {
                uint64_t dependent_submit = binary.waitingSubmits.front();
                binary.waitingSubmits.pop_front();
                SubmitInfo& dependent_submit_info = submit_entries_[dependent_submit];

                bool allReady = true;
                // whether all the wait_sem are signaled
                for (auto handle : dependent_submit_info.wait_semaphores)
                {
                    if (!std::get<BinarySemaphoreState>(semaphore_state[handle].state).signaled)
                    {
                        allReady = false;
                        break;
                    }
                }
                if (allReady && !dependent_submit_info.ready)
                {
                    dependent_submit_info.ready = true;
                    ready_submits.push_back(dependent_submit);
                }
            }
        }
    }
}

void VulkanSpirvTrackModifier::Process_vkQueueSubmit(const ApiCallInfo&                          call_info,
                                                     VkResult                                    returnValue,
                                                     format::HandleId                            queue,
                                                     uint32_t                                    submitCount,
                                                     StructPointerDecoder<Decoded_VkSubmitInfo>* pSubmits,
                                                     format::HandleId                            fence)
{
    if (IsModificationPass())
    {
        return;
    }

    const auto* meta_submit_infos = pSubmits->GetMetaStructPointer();
    const auto* submit_infos      = pSubmits->GetPointer();

    std::vector<format::HandleId> command_buffer;
    for (uint32_t submit_index = 0; submit_index < submitCount; submit_index++)
    {
        for (uint32_t cmdBuffer_index = 0; cmdBuffer_index < submit_infos[submit_index].commandBufferCount;
             cmdBuffer_index++)
        {
            format::HandleId cmdBuf = meta_submit_infos[submit_index].pCommandBuffers.GetPointer()[cmdBuffer_index];
            command_buffer.push_back(cmdBuf);
            command_buffer_entries_.at(cmdBuf).state = CommandBufferLifeCycle::Pending;

            if (command_buffer_recording[cmdBuf].in_operation &&
                command_buffer_recording[cmdBuf].bind_point == VK_PIPELINE_BIND_POINT_MAX_ENUM)
            {
                command_buffer_submit_recordings[cmdBuf].emplace_back(command_buffer_recording[cmdBuf]);
            }
            command_buffer_recording.erase(cmdBuf);
        }

        process_SubmitInfo(global_submit_index++, command_buffer);
        command_buffer.clear();
    }

    while (!ready_submits.empty())
    {
        uint64_t submit_id = ready_submits.front();
        ready_submits.pop_front();

        SubmitInfo& submit_info = submit_entries_[submit_id];
        if (submit_info.submitted)
        {
            continue;
        }
        submit_info.submitted = true;

        // execute each command buffer in the submit
        for (auto& id : submit_info.command_buffers)
        {
            executeCommandBuffer(id);
        }

        // signalSemaphoresFrom(submit_id);
        submit_entries_.erase(submit_id);
    }
}

void VulkanSpirvTrackModifier::Process_vkQueueSubmit2(const ApiCallInfo&                           call_info,
                                                      VkResult                                     returnValue,
                                                      format::HandleId                             queue,
                                                      uint32_t                                     submitCount,
                                                      StructPointerDecoder<Decoded_VkSubmitInfo2>* pSubmits,
                                                      format::HandleId                             fence)
{
    if (IsModificationPass())
    {
        return;
    }
}

void VulkanSpirvTrackModifier::Process_vkQueueSubmit2KHR(const ApiCallInfo&                           call_info,
                                                         VkResult                                     returnValue,
                                                         format::HandleId                             queue,
                                                         uint32_t                                     submitCount,
                                                         StructPointerDecoder<Decoded_VkSubmitInfo2>* pSubmits,
                                                         format::HandleId                             fence)
{}

void VulkanSpirvTrackModifier::resetRecording(format::HandleId commandBuffer)
{
    command_buffer_recording[commandBuffer].bind_point   = VK_PIPELINE_BIND_POINT_MAX_ENUM;
    command_buffer_recording[commandBuffer].in_operation = false;
    command_buffer_recording[commandBuffer].push_constants.clear();
    command_buffer_recording[commandBuffer].pipelines.clear();
    command_buffer_recording[commandBuffer].descriptor_state_commands.clear();
    command_buffer_recording[commandBuffer].buffer_write_list.clear();
}

void VulkanSpirvTrackModifier::ApplyActionCommands(const CommandBufferRecording& recording)
{
    // execute command: update/copy buffer, build
    for (const BufferWriteEvent& event : recording.buffer_write_list)
    {
        BufferInfo& dst_info = buffer_entries_.at(event.buffer);
        switch (event.sourceType)
        {
            case SourceType::CopyBuffer:
            {
                const BufferInfo& src_info = buffer_entries_.at(event.srcBuffer);
                for (const auto& region : event.regions)
                {
                    GFXRECON_ASSERT(dst_info.size >= region.dstOffset + region.size);
                    GFXRECON_ASSERT(src_info.size >= region.srcOffset + region.size);
                    std::memcpy(
                        dst_info.data.data() + region.dstOffset, src_info.data.data() + region.srcOffset, region.size);
                }
                break;
            }
            case SourceType::Update:
            {
                VkDeviceSize offset = event.regions[0].dstOffset;
                VkDeviceSize size   = event.regions[0].size;
                GFXRECON_ASSERT(dst_info.size >= offset + size);
                std::memcpy(dst_info.data.data() + offset, event.srcPointer.data() + event.regions[0].srcOffset, size);
                break;
            }
            case SourceType::Fill:
            {
                VkDeviceSize offset  = event.regions[0].dstOffset;
                VkDeviceSize size    = event.regions[0].size;
                uint8_t*     dst_ptr = dst_info.data.data() + offset;
                if (size == VK_WHOLE_SIZE)
                {
                    size = dst_info.size - offset;
                }
                for (uint32_t i = 0; i < size / 4; i++)
                {
                    std::memcpy(dst_ptr + i * 4, event.srcPointer.data(), sizeof(uint32_t));
                }
                break;
            }
            default:
                break;
        }
    }
}

bool VulkanSpirvTrackModifier::IsPipelineLayoutCompatibleForSet(format::HandleId lhs,
                                                                format::HandleId rhs,
                                                                uint32_t         set) const
{
    if (lhs == rhs)
    {
        return true;
    }

    const auto lhs_iter = pipeline_layout_entries_.find(lhs);
    const auto rhs_iter = pipeline_layout_entries_.find(rhs);
    if (lhs_iter == pipeline_layout_entries_.end() || rhs_iter == pipeline_layout_entries_.end())
    {
        return false;
    }

    const auto& lhs_info = lhs_iter->second;
    const auto& rhs_info = rhs_iter->second;

    // Push constant compatibility is order-insensitive, but still requires identical ranges.
    if (lhs_info.pushConstantRanges.size() != rhs_info.pushConstantRanges.size())
    {
        return false;
    }

    auto lhs_push_constant_ranges    = lhs_info.pushConstantRanges;
    auto rhs_push_constant_ranges    = rhs_info.pushConstantRanges;
    auto compare_push_constant_range = [](const VkPushConstantRange& lhs_range, const VkPushConstantRange& rhs_range) {
        if (lhs_range.offset != rhs_range.offset)
        {
            return lhs_range.offset < rhs_range.offset;
        }
        if (lhs_range.size != rhs_range.size)
        {
            return lhs_range.size < rhs_range.size;
        }
        return lhs_range.stageFlags < rhs_range.stageFlags;
    };
    std::sort(lhs_push_constant_ranges.begin(), lhs_push_constant_ranges.end(), compare_push_constant_range);
    std::sort(rhs_push_constant_ranges.begin(), rhs_push_constant_ranges.end(), compare_push_constant_range);

    for (size_t i = 0; i < lhs_push_constant_ranges.size(); ++i)
    {
        const VkPushConstantRange& lhs_range = lhs_push_constant_ranges[i];
        const VkPushConstantRange& rhs_range = rhs_push_constant_ranges[i];
        if (lhs_range.stageFlags != rhs_range.stageFlags || lhs_range.offset != rhs_range.offset ||
            lhs_range.size != rhs_range.size)
        {
            return false;
        }
    }

    // create independent_sets flag compatible
    const VkPipelineLayoutCreateFlags independent_sets_flag = VK_PIPELINE_LAYOUT_CREATE_INDEPENDENT_SETS_BIT_EXT;
    if ((lhs_info.flags & independent_sets_flag) != (rhs_info.flags & independent_sets_flag))
    {
        return false;
    }

    // setlayout compatible
    if (lhs_info.setLayouts.size() <= set || rhs_info.setLayouts.size() <= set)
    {
        return false;
    }

    for (uint32_t index = 0; index <= set; ++index)
    {
        const auto lhs_set_layout_iter = set_layout_entries_.find(lhs_info.setLayouts[index]);
        const auto rhs_set_layout_iter = set_layout_entries_.find(rhs_info.setLayouts[index]);
        if (lhs_set_layout_iter == set_layout_entries_.end() || rhs_set_layout_iter == set_layout_entries_.end())
        {
            return false;
        }

        const auto& lhs_set_layout_info = lhs_set_layout_iter->second;
        const auto& rhs_set_layout_info = rhs_set_layout_iter->second;
        if (lhs_set_layout_info.flags != rhs_set_layout_info.flags ||
            lhs_set_layout_info.bindings.size() != rhs_set_layout_info.bindings.size())
        {
            return false;
        }

        auto lhs_binding_iter = lhs_set_layout_info.bindings.begin();
        auto rhs_binding_iter = rhs_set_layout_info.bindings.begin();
        while (lhs_binding_iter != lhs_set_layout_info.bindings.end())
        {
            const Binding& lhs_binding = lhs_binding_iter->second;
            const Binding& rhs_binding = rhs_binding_iter->second;
            if (lhs_binding.binding != rhs_binding.binding || lhs_binding.type != rhs_binding.type ||
                lhs_binding.mutable_descriptor_types != rhs_binding.mutable_descriptor_types ||
                lhs_binding.descriptorCount != rhs_binding.descriptorCount ||
                lhs_binding.stageFlags != rhs_binding.stageFlags ||
                lhs_binding.binding_flags != rhs_binding.binding_flags)
            {
                return false;
            }

            ++lhs_binding_iter;
            ++rhs_binding_iter;
        }
    }

    return true;
}

void VulkanSpirvTrackModifier::ApplyDescriptorStateCommands(format::HandleId              commandBuffer,
                                                            const CommandBufferRecording& recording)
{
    CommandBufferState& current_command_buffer_state = command_buffer_state[commandBuffer];

    auto clear_descriptor_set_backend_state = [](BindPointState& state) {
        state.descriptor_set_map.clear();
        state.dynamic_offsets.clear();
        state.dynamic_offsets_perSet.clear();
        state.dynamic_offsets_count.clear();
    };

    auto clear_descriptor_buffer_backend_state = [](BindPointState& state) {
        state.descriptor_buffer_set_offset_map.clear();
        // TODO: clear descriptor heap backend state when it is introduced.
    };

    auto erase_descriptor_set_binding = [](BindPointState& state, uint32_t set) {
        state.descriptor_set_map.erase(set);
        state.dynamic_offsets_perSet.erase(set);
        state.dynamic_offsets_count.erase(set);
    };

    auto erase_descriptor_buffer_set_offset = [](BindPointState& state, uint32_t set) {
        state.descriptor_buffer_set_offset_map.erase(set);
    };

    for (const DescriptorStateCommand& descriptor_command : recording.descriptor_state_commands)
    {
        switch (descriptor_command.type)
        {
            case DescriptorStateCommand::Type::BindDescriptorSets:
            {
                auto& current_bind_point_state =
                    current_command_buffer_state.bind_point_state[descriptor_command.bind_point];

                if (current_bind_point_state.descriptor_backend_mode != DescriptorBackendMode::DescriptorSet)
                {
                    clear_descriptor_buffer_backend_state(current_bind_point_state);
                    // TODO: clear descriptor heap backend state when it is introduced.
                }
                current_bind_point_state.descriptor_backend_mode = DescriptorBackendMode::DescriptorSet;

                for (const auto& map : descriptor_command.descriptor_sets)
                {
                    // check compatible for each set M < map.set
                    std::vector<uint32_t> disturbed_lower_sets;
                    for (const auto& bound_set_iter : current_bind_point_state.descriptor_set_map)
                    {
                        if (bound_set_iter.first < map.set &&
                            !IsPipelineLayoutCompatibleForSet(
                                bound_set_iter.second.layout, map.layout, bound_set_iter.first))
                        {
                            disturbed_lower_sets.push_back(bound_set_iter.first);
                        }
                    }
                    for (uint32_t disturbed_set : disturbed_lower_sets)
                    {
                        erase_descriptor_set_binding(current_bind_point_state, disturbed_set);
                    }

                    // check compatible for map.set
                    const auto current_set_iter = current_bind_point_state.descriptor_set_map.find(map.set);
                    if (current_set_iter != current_bind_point_state.descriptor_set_map.end() &&
                        !IsPipelineLayoutCompatibleForSet(current_set_iter->second.layout, map.layout, map.set))
                    {
                        std::vector<uint32_t> disturbed_higher_sets;
                        for (const auto& bound_set_iter : current_bind_point_state.descriptor_set_map)
                        {
                            if (bound_set_iter.first > map.set)
                            {
                                disturbed_higher_sets.push_back(bound_set_iter.first);
                            }
                        }
                        for (uint32_t disturbed_set : disturbed_higher_sets)
                        {
                            erase_descriptor_set_binding(current_bind_point_state, disturbed_set);
                        }
                    }

                    // set current map.set
                    erase_descriptor_set_binding(current_bind_point_state, map.set);
                    current_bind_point_state.descriptor_set_map[map.set] = { map.descriptor_set, map.layout };

                    const auto dynamic_offset_iter = descriptor_command.dynamic_offsets.find(map.set);
                    if (dynamic_offset_iter != descriptor_command.dynamic_offsets.end())
                    {
                        std::vector<uint32_t> offsets;
                        for (const auto& offset_iter : dynamic_offset_iter->second.binding_offsets)
                        {
                            auto pos = offsets.end();
                            offsets.insert(pos, offset_iter.second.begin(), offset_iter.second.end());
                            current_bind_point_state.dynamic_offsets_count[map.set][offset_iter.first] =
                                offset_iter.second.size();
                        }
                        current_bind_point_state.dynamic_offsets_perSet[map.set] = std::move(offsets);
                    }
                }

                current_bind_point_state.dynamic_offsets.clear();
                for (const auto& it : current_bind_point_state.dynamic_offsets_perSet)
                {
                    auto pos = current_bind_point_state.dynamic_offsets.end();
                    current_bind_point_state.dynamic_offsets.insert(pos, it.second.begin(), it.second.end());
                }
                break;
            }
            case DescriptorStateCommand::Type::BindDescriptorBuffers:
            {
                current_command_buffer_state.descriptor_buffers = descriptor_command.descriptor_buffers;

                for (auto& bind_point_state_iter : current_command_buffer_state.bind_point_state)
                {
                    bind_point_state_iter.second.descriptor_buffer_set_offset_map.clear();
                }
                break;
            }
            case DescriptorStateCommand::Type::SetDescriptorBufferOffsets:
            {
                auto& current_bind_point_state =
                    current_command_buffer_state.bind_point_state[descriptor_command.bind_point];

                if (current_bind_point_state.descriptor_backend_mode != DescriptorBackendMode::DescriptorBuffer)
                {
                    clear_descriptor_set_backend_state(current_bind_point_state);
                    // TODO: clear descriptor heap backend state when it is introduced.
                }
                current_bind_point_state.descriptor_backend_mode = DescriptorBackendMode::DescriptorBuffer;

                for (const DescriptorBufferOffsetMap& map : descriptor_command.descriptor_buffer_offsets)
                {
                    // check compatible for each set M < map.set
                    std::vector<uint32_t> disturbed_lower_sets;
                    for (const auto& bound_set_iter : current_bind_point_state.descriptor_buffer_set_offset_map)
                    {
                        if (bound_set_iter.first < map.set &&
                            !IsPipelineLayoutCompatibleForSet(
                                bound_set_iter.second.layout, map.layout, bound_set_iter.first))
                        {
                            disturbed_lower_sets.push_back(bound_set_iter.first);
                        }
                    }
                    for (uint32_t disturbed_set : disturbed_lower_sets)
                    {
                        erase_descriptor_buffer_set_offset(current_bind_point_state, disturbed_set);
                    }

                    // check compatible for map.set
                    const auto current_set_iter =
                        current_bind_point_state.descriptor_buffer_set_offset_map.find(map.set);
                    if (current_set_iter != current_bind_point_state.descriptor_buffer_set_offset_map.end() &&
                        !IsPipelineLayoutCompatibleForSet(current_set_iter->second.layout, map.layout, map.set))
                    {
                        std::vector<uint32_t> disturbed_higher_sets;
                        for (const auto& bound_set_iter : current_bind_point_state.descriptor_buffer_set_offset_map)
                        {
                            if (bound_set_iter.first > map.set)
                            {
                                disturbed_higher_sets.push_back(bound_set_iter.first);
                            }
                        }
                        for (uint32_t disturbed_set : disturbed_higher_sets)
                        {
                            erase_descriptor_buffer_set_offset(current_bind_point_state, disturbed_set);
                        }
                    }

                    GFXRECON_ASSERT(map.buffer_index < current_command_buffer_state.descriptor_buffers.size());

                    // set current map.set
                    const auto& buffer_info = current_command_buffer_state.descriptor_buffers[map.buffer_index];
                    erase_descriptor_buffer_set_offset(current_bind_point_state, map.set);
                    if (!buffer_info.resolved)
                    {
                        continue;
                    }
                    current_bind_point_state.descriptor_buffer_set_offset_map[map.set] = {
                        buffer_info.handle, buffer_info.base_address, buffer_info.address, map.offset, map.layout
                    };
                }
                break;
            }
            default:
                break;
        }
    }
}

void VulkanSpirvTrackModifier::executeCommandBuffer(format::HandleId commandBuffer)
{
    if (command_buffer_submit_recordings.find(commandBuffer) == command_buffer_submit_recordings.end())
    {
        command_buffer_recording.erase(commandBuffer);
        command_buffer_entries_.at(commandBuffer).state = CommandBufferLifeCycle::Executable;
        return;
    }

    for (const CommandBufferRecording& recording : command_buffer_submit_recordings[commandBuffer])
    {
        ApplyActionCommands(recording);

        // update commandBuffer state
        CommandBufferState& current_command_buffer_state = command_buffer_state[commandBuffer];
        current_command_buffer_state.command_buffer      = commandBuffer;

        for (const PushConstantData& pushconstant : recording.push_constants)
        {
            std::memcpy(current_command_buffer_state.push_constant.data() + pushconstant.offset,
                        pushconstant.pValues.data(),
                        pushconstant.size);
        }

        for (const auto& pipeline_iter : recording.pipelines)
        {
            current_command_buffer_state.bind_point_state[pipeline_iter.first].pipeline = pipeline_iter.second;
            current_command_buffer_state.bind_point_state[pipeline_iter.first].pipeline_layout =
                pipeline_entries_[pipeline_iter.second].layout;
        }

        ApplyDescriptorStateCommands(commandBuffer, recording);

        // execute dispatch,draw
        if (recording.bind_point == VK_PIPELINE_BIND_POINT_COMPUTE ||
            recording.bind_point == VK_PIPELINE_BIND_POINT_GRAPHICS)
        {
            executeDispatchDraw(commandBuffer, recording.bind_point);
        }
    }
    command_buffer_submit_recordings.erase(commandBuffer);
    command_buffer_entries_.at(commandBuffer).state = CommandBufferLifeCycle::Executable;
}

void VulkanSpirvTrackModifier::executeDispatchDraw(format::HandleId commandBuffer, VkPipelineBindPoint bindPoint)
{
    const BindPointState& bind_point_state = command_buffer_state[commandBuffer].bind_point_state.at(bindPoint);

    auto is_dynamic_buffer_descriptor_type = [](VkDescriptorType descriptor_type) {
        return descriptor_type == VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC ||
               descriptor_type == VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC;
    };

    auto is_static_buffer_descriptor_type = [](VkDescriptorType descriptor_type) {
        return descriptor_type == VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER ||
               descriptor_type == VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    };

    auto get_active_descriptor_type = [](const DescriptorArray& descriptor_array, const DescriptorEntry& desc_entry) {
        return (descriptor_array.type == VK_DESCRIPTOR_TYPE_MUTABLE_EXT) ? desc_entry.type : descriptor_array.type;
    };

    auto is_buffer_back_descriptor_binding = [&](const DescriptorArray& descriptor_array) {
        return is_static_buffer_descriptor_type(descriptor_array.type) ||
               is_dynamic_buffer_descriptor_type(descriptor_array.type) ||
               descriptor_array.type == VK_DESCRIPTOR_TYPE_MUTABLE_EXT;
    };

    // At the sim_data.bindings boundary, gfxr currently collapses several "no usable payload pointer"
    // cases to nullptr, including unbound / never-written slots, null descriptors and tracked resources
    // that cannot be resolved to host shadow data.
    // The current simulator integration does not distinguish these cases at the sim_data.bindings boundary.
    // A nullptr is treated as "payload unavailable" and is intentionally conservative for the current data-flow
    // behavior.
    auto resolve_bound_buffer_descriptor_pointer = [&](uint32_t               set,
                                                       const DescriptorArray& descriptor_array,
                                                       uint32_t               array_index,
                                                       const DescriptorEntry& desc_entry,
                                                       uint32_t               dynamic_offset_base) -> void* {
        const VkDescriptorType active_type = get_active_descriptor_type(descriptor_array, desc_entry);
        if (!is_static_buffer_descriptor_type(active_type) && !is_dynamic_buffer_descriptor_type(active_type))
        {
            return nullptr;
        }

        const auto buffer_iter = buffer_entries_.find(desc_entry.buffer.handle);
        if (buffer_iter == buffer_entries_.end())
        {
            return nullptr;
        }

        uint8_t* binding_ptr = buffer_iter->second.data.data() + desc_entry.buffer.offset;
        if (is_dynamic_buffer_descriptor_type(descriptor_array.type))
        {
            binding_ptr += bind_point_state.dynamic_offsets_perSet.at(set).at(dynamic_offset_base + array_index);
        }

        return binding_ptr;
    };

    std::string str = "Not support bindPoint";
    if (bindPoint == VK_PIPELINE_BIND_POINT_COMPUTE)
    {
        str = "CmdDispatch";
    }
    else if (bindPoint == VK_PIPELINE_BIND_POINT_GRAPHICS)
    {
        str = "CmdDraw";
    }
    GFXRECON_LOG_INFO(" ---------------------------- %llu: Pipeline %llu Execute %s ----------------------------",
                      global_draw_index++,
                      bind_point_state.pipeline,
                      str.c_str());

    const auto& pipeline_info        = pipeline_entries_.at(bind_point_state.pipeline);
    const auto  pipeline_layout_iter = pipeline_layout_entries_.find(bind_point_state.pipeline_layout);

    auto is_set_slot_accessible = [&](uint32_t set, format::HandleId bound_layout) {
        return pipeline_layout_iter != pipeline_layout_entries_.end() &&
               IsPipelineLayoutCompatibleForSet(bound_layout, bind_point_state.pipeline_layout, set);
    };

    SPIRVSimulator::SimulationData                                                 sim_data;
    std::unordered_map<uint64_t, std::unordered_map<uint64_t, std::vector<void*>>> sim_binding_pointer_tables;

    if (bind_point_state.descriptor_backend_mode == DescriptorBackendMode::DescriptorSet)
    {
        // set up descriptors through descriptorSet
        for (const auto& set_map_iter : bind_point_state.descriptor_set_map)
        {
            uint32_t set = set_map_iter.first;
            if (!is_set_slot_accessible(set, set_map_iter.second.layout))
            {
                if (m_verbose)
                {
                    GFXRECON_LOG_INFO("Set[%u]: skipped, not compatible with current pipeline layout.", set);
                }
                continue;
            }

            const auto& descriptor_set_info  = descriptor_set_entries_.at(set_map_iter.second.descriptor_set);
            uint32_t    dynamic_offset_index = 0;

            for (const auto& binding_iter : descriptor_set_info.binding_descriptor_array)
            {
                uint64_t    binding          = binding_iter.first;
                const auto& descriptor_array = binding_iter.second;

                uint32_t alloc_descriptor_count = descriptor_array.descriptorCount;
                if (is_buffer_back_descriptor_binding(descriptor_array))
                {
                    const uint32_t dynamic_offset_base = dynamic_offset_index;
                    if (is_dynamic_buffer_descriptor_type(descriptor_array.type))
                    {
                        dynamic_offset_index += alloc_descriptor_count;
                    }

                    if (alloc_descriptor_count == 1)
                    {
                        void*      binding_ptr     = nullptr;
                        const auto desc_entry_iter = descriptor_array.descriptor_entries.find(0);
                        if (desc_entry_iter != descriptor_array.descriptor_entries.end())
                        {
                            binding_ptr = resolve_bound_buffer_descriptor_pointer(
                                set, descriptor_array, 0, desc_entry_iter->second, dynamic_offset_base);
                        }
                        sim_data.bindings[set][binding] = binding_ptr;
                    }
                    else if (alloc_descriptor_count > 1)
                    {
                        auto& pointer_array = sim_binding_pointer_tables[set][binding];
                        pointer_array.assign(alloc_descriptor_count, nullptr);
                        for (const auto& entry_iter : descriptor_array.descriptor_entries)
                        {
                            if (entry_iter.first >= alloc_descriptor_count)
                            {
                                continue;
                            }

                            pointer_array[entry_iter.first] = resolve_bound_buffer_descriptor_pointer(
                                set, descriptor_array, entry_iter.first, entry_iter.second, dynamic_offset_base);
                        }
                        sim_data.bindings[set][binding] = pointer_array.data();
                    }
                }
                else if (descriptor_array.type == VK_DESCRIPTOR_TYPE_INLINE_UNIFORM_BLOCK)
                {
                    uint8_t* binding_ptr = nullptr;
                    if (!descriptor_array.inline_uniform_block_data.empty())
                    {
                        binding_ptr = const_cast<uint8_t*>(descriptor_array.inline_uniform_block_data.data());
                    }
                    sim_data.bindings[set][binding] = binding_ptr;
                }
                else if (descriptor_array.type == VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR)
                {}
                else
                {}
            }
        }
    }

    if (bind_point_state.descriptor_backend_mode == DescriptorBackendMode::DescriptorBuffer)
    {
        // get binding offset within set offset
        for (const auto& map_iter : bind_point_state.descriptor_buffer_set_offset_map)
        {
            uint32_t   set        = map_iter.first;
            uint64_t   binding    = 0;
            const auto set_offset = map_iter.second;

            if (!is_set_slot_accessible(set, set_offset.layout))
            {
                if (m_verbose)
                {
                    GFXRECON_LOG_INFO("Set[%u]: skipped, not compatible with current pipeline layout.", set);
                }
                continue;
            }

            const auto& pipeline_layout_info = pipeline_layout_entries_.at(set_offset.layout);
            const auto& set_layout_info      = set_layout_entries_.at(pipeline_layout_info.setLayouts[set]);
            GFXRECON_ASSERT(set_layout_info.flags == VK_DESCRIPTOR_SET_LAYOUT_CREATE_DESCRIPTOR_BUFFER_BIT_EXT);

            for (const auto& binding_iter : set_layout_info.bindings)
            {
                binding = binding_iter.first;
                // TODO: handle arrayElement if the more elements in descriptor array needed.
                VkDeviceSize binding_offset =
                    (set_offset.address - set_offset.base_address) + set_offset.offset + binding_iter.second.offset;
                const auto& buffer_info = buffer_entries_.at(set_offset.handle);

                uint8_t* data_host_pointer      = (uint8_t*)buffer_info.data.data() + binding_offset;
                sim_data.bindings[set][binding] = data_host_pointer;
                // sim_data.descriptor_candidates[data_host_pointer].emplace_back({});
                if (binding_iter.second.type == VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER ||
                    binding_iter.second.type == VK_DESCRIPTOR_TYPE_STORAGE_BUFFER)
                {}
                else if (binding_iter.second.type == VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR)
                {}
            }
        }
    }

    // physical_address_buffers
    for (const auto& it : buffer_device_addresses_)
    {
        sim_data.physical_address_buffers[it.first] = { buffer_entries_[it.second].size,
                                                        buffer_entries_[it.second].data.data() };
    }

    for (const ShaderStage& stage : pipeline_info.stages)
    {
        if (stage.stageFlagBit != VK_SHADER_STAGE_COMPUTE_BIT && stage.stageFlagBit != VK_SHADER_STAGE_FRAGMENT_BIT &&
            stage.stageFlagBit != VK_SHADER_STAGE_VERTEX_BIT)
        {
            continue;
        }

        sim_data.entry_point_op_name = stage.pName;

        // specialization constant
        sim_data.specialization_constants = stage.specialization_constant.data();
        sim_data.specialization_constant_offsets.clear();
        for (const auto& entry : stage.specialization_offset_entries)
        {
            sim_data.specialization_constant_offsets[entry.first] = entry.second;
        }

        // push constant
        sim_data.push_constants = nullptr;
        if (pipeline_layout_iter != pipeline_layout_entries_.end())
        {
            const auto& pipeline_layout_info = pipeline_layout_entries_.at(bind_point_state.pipeline_layout);
            if (pipeline_layout_info.mergedRangePerStage.find(stage.stageFlagBit) !=
                pipeline_layout_info.mergedRangePerStage.end())
            {
                sim_data.push_constants = command_buffer_state[commandBuffer].push_constant.data() +
                                          pipeline_layout_info.mergedRangePerStage.at(stage.stageFlagBit).offset;
            }
        }

        const ShaderModuleInfo& module_info = shader_module_entries_.at(stage.module);

        GFXRECON_LOG_INFO("     --------- run simulator for shader %llu: %s -------------",
                          stage.module,
                          util::ToString<VkShaderStageFlagBits>(stage.stageFlagBit).c_str());
        SPIRVSimulator::SimulationResults sim_results;
        SPIRVSimulator::SPIRVSimulator    simulator(
            module_info.pCode, &sim_data, &sim_results, nullptr, m_verbose, m_flags);
        simulator.Run();
        outputSimulator(sim_results);
    }
}

void VulkanSpirvTrackModifier::outputSimulator(const SPIRVSimulator::SimulationResults& results)
{
    auto physical_address_data = results.physical_address_data;

    GFXRECON_LOG_INFO("     >>>>>>>>>>>>> Pointers to pbuffers: >>>>>>>>>>>>>");
    for (const auto& pointer_t : physical_address_data)
    {
        GFXRECON_LOG_INFO("  Found pointer with address: 0x%lx, made from input bit components: ",
                          pointer_t.raw_pointer_value);
        for (auto bit_component : pointer_t.bit_components)
        {
            if (bit_component.location == SPIRVSimulator::BitLocation::Constant)
            {
                GFXRECON_LOG_INFO("    From Constant in SPIRV input words, at Byte Offset: %llu",
                                  bit_component.byte_offset);
            }
            else if (bit_component.location == SPIRVSimulator::BitLocation::SpecConstant)
            {
                GFXRECON_LOG_INFO("    From SpecId: %llu, Byte Offset: %llu, Bitsize: %llu, to val Bit Offset: %llu.",
                                  bit_component.binding_id,
                                  bit_component.byte_offset,
                                  bit_component.bitcount,
                                  bit_component.val_bit_offset);
            }
            else // storageClass
            {
                GFXRECON_LOG_INFO("    From DescriptorSetID: %llu, Binding: %llu, in StorageClass %s, Byte Offset: "
                                  "%llu, Bitsize: %llu, to val Bit Offset: %llu.",
                                  bit_component.set_id,
                                  bit_component.binding_id,
                                  spv::StorageClassToString(bit_component.storage_class),
                                  bit_component.byte_offset,
                                  bit_component.bitcount,
                                  bit_component.val_bit_offset);
            }
        }
    }
    std::cout << "\n" << std::endl;
}

GFXRECON_END_NAMESPACE(decode)
GFXRECON_END_NAMESPACE(gfxrecon)