/*
** Copyright (c) 2026 LunarG, Inc.
** Copyright (c) 2026 Arm Limited and/or its affiliates <open-source-office@arm.com>
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

#ifndef GFXRECON_TOOLS_OPTIMIZE_VULKAN_ARM_TRACE_HELPERS_MODIFIER_H
#define GFXRECON_TOOLS_OPTIMIZE_VULKAN_ARM_TRACE_HELPERS_MODIFIER_H

#include <vector>
#include <vulkan/vulkan_core.h>

#include "decode/api_decoder.h"
#include "format/format.h"
#include "util/defines.h"
#include "util/vulkan_modifier_base.h"

GFXRECON_BEGIN_NAMESPACE(gfxrecon)
GFXRECON_BEGIN_NAMESPACE(decode)

class VulkanArmTraceHelpersModifier : public util::VulkanModifierBase
{
  public:
    VulkanArmTraceHelpersModifier() = default;

    virtual bool CanOptimize() override;

    virtual void Process_vkCreateDevice(const ApiCallInfo&                                   call_info,
                                        VkResult                                             returnValue,
                                        format::HandleId                                     physicalDevice,
                                        StructPointerDecoder<Decoded_VkDeviceCreateInfo>*    pCreateInfo,
                                        StructPointerDecoder<Decoded_VkAllocationCallbacks>* pAllocator,
                                        HandlePointerDecoder<VkDevice>*                      pDevice) override;

    virtual void Process_vkAllocateMemory(const ApiCallInfo&                                   call_info,
                                          VkResult                                             returnValue,
                                          format::HandleId                                     device,
                                          StructPointerDecoder<Decoded_VkMemoryAllocateInfo>*  pAllocateInfo,
                                          StructPointerDecoder<Decoded_VkAllocationCallbacks>* pAllocator,
                                          HandlePointerDecoder<VkDeviceMemory>*                pMemory) override;

    virtual void
    Process_vkFlushMappedMemoryRanges(const ApiCallInfo&                                 call_info,
                                      VkResult                                           returnValue,
                                      format::HandleId                                   device,
                                      uint32_t                                           memoryRangeCount,
                                      StructPointerDecoder<Decoded_VkMappedMemoryRange>* pMemoryRanges) override;

    virtual void
    ProcessFillMemoryCommand(uint64_t memory_id, uint64_t offset, uint64_t size, const uint8_t* data) override;

  private:
    VkDeviceSize GetDataSizeBasedOnHelpersType(const VkMarkingTypeARM&    marking_types,
                                               const VkMarkingSubTypeARM& sub_types);

  private:
    struct FillMemoryCommandEntry
    {
        format::HandleId     memory;
        VkDeviceSize         offset;
        std::vector<uint8_t> data;
    };

    struct PropagateData
    {
        format::HandleId     memory;
        VkDeviceSize         absolute_offset;
        std::vector<uint8_t> capture_time_data_propagate;
        VkMarkingTypeARM     marking_types;
        VkMarkingSubTypeARM  sub_types;
    };

    std::vector<FillMemoryCommandEntry> previous_fill_memory_commands;
    std::vector<PropagateData>          struct_to_propagate_;

    std::vector<format::HandleId> devices_using_helpers;
    std::vector<format::HandleId> memories_using_helpers;
};

GFXRECON_END_NAMESPACE(decode)
GFXRECON_END_NAMESPACE(gfxrecon)

#endif // GFXRECON_TOOLS_OPTIMIZE_VULKAN_ARM_TRACE_HELPERS_MODIFIER_H
