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

#ifndef GFXRECON_TOOLS_OPTIMIZE_RESOURCE_MEMORY_REQUIREMENTS_MODIFIER_H
#define GFXRECON_TOOLS_OPTIMIZE_RESOURCE_MEMORY_REQUIREMENTS_MODIFIER_H

#include <cstdint>
#include <limits>
#include <unordered_map>
#include <vector>
#include <vulkan/vulkan_core.h>

#include "decode/api_decoder.h"
#include "format/format_arm.h"
#include "format/format.h"
#include "util/defines.h"
#include "util/vulkan_modifier_base.h"

GFXRECON_BEGIN_NAMESPACE(gfxrecon)
GFXRECON_BEGIN_NAMESPACE(decode)

// Tracks resource lifetime/bind information and emits aliasing metadata consumed during replay.
class ResourceMemoryRequirementsModifier : public util::VulkanModifierBase
{
  public:
    ResourceMemoryRequirementsModifier()           = default;
    ~ResourceMemoryRequirementsModifier() override = default;

    bool CanOptimize() override;

    void Process_vkCreateDevice(const ApiCallInfo& call_info, args::CreateDevice& args) override;

    void Process_vkCreateBuffer(const ApiCallInfo& call_info, args::CreateBuffer& args) override;

    void Process_vkDestroyBuffer(const ApiCallInfo& call_info, args::DestroyBuffer& args) override;

    void Process_vkCreateImage(const ApiCallInfo& call_info, args::CreateImage& args) override;

    void Process_vkDestroyImage(const ApiCallInfo& call_info, args::DestroyImage& args) override;

    void Process_vkCreateTensorARM(const ApiCallInfo& call_info, args::CreateTensorARM& args) override;

    void Process_vkDestroyTensorARM(const ApiCallInfo& call_info, args::DestroyTensorARM& args) override;

    void Process_vkBindBufferMemory(const ApiCallInfo& call_info, args::BindBufferMemory& args) override;

    void Process_vkBindBufferMemory2(const ApiCallInfo& call_info, args::BindBufferMemory2& args) override;

    void Process_vkBindBufferMemory2KHR(const ApiCallInfo& call_info, args::BindBufferMemory2KHR& args) override;

    void Process_vkBindImageMemory(const ApiCallInfo& call_info, args::BindImageMemory& args) override;

    void Process_vkBindImageMemory2(const ApiCallInfo& call_info, args::BindImageMemory2& args) override;

    void Process_vkBindImageMemory2KHR(const ApiCallInfo& call_info, args::BindImageMemory2KHR& args) override;

    void Process_vkBindTensorMemoryARM(const ApiCallInfo& call_info, args::BindTensorMemoryARM& args) override;

  private:
    struct ResourceMemoryRequirementsInfo
    {
        format::arm::ResourceMemoryRequirementsPropertiesResourceType resource_type{};
        uint8_t                                                       aliasing_group{};
        // Call index where resource became bound to memory (0 means unbound).
        uint64_t bind_index{};
        // Byte offset into memory at bind time.
        uint64_t         bind_offset{};
        format::HandleId resource_handle{ format::kNullHandleId };
        format::HandleId memory_handle{ format::kNullHandleId };
        // Defaults to "alive forever" until a destroy call is observed.
        uint64_t                destroy_index{ std::numeric_limits<uint64_t>::max() };
        encode::ParameterBuffer encoded_create_info{};
    };
    std::unordered_map<format::HandleId, std::vector<ResourceMemoryRequirementsInfo>>
        resources_memory_requirements_by_device_;
    std::unordered_map<format::HandleId, std::unordered_map<format::HandleId, size_t>> resource_index_by_device_;

    ResourceMemoryRequirementsInfo* FindResourceInfo(format::HandleId device_id, format::HandleId resource_id);
    void                            TrackBind(format::HandleId device_id,
                                              format::HandleId resource_id,
                                              format::HandleId memory_id,
                                              VkDeviceSize     memory_offset,
                                              uint64_t         bind_index,
                                              const char*      resource_name);
    void OnResourceDestroyCall(format::HandleId device_id, format::HandleId resource_id, uint64_t call_index);
    void GenerateAliasingGroups(std::vector<ResourceMemoryRequirementsInfo>* resources);
};

GFXRECON_END_NAMESPACE(decode)
GFXRECON_END_NAMESPACE(gfxrecon)

#endif // GFXRECON_TOOLS_OPTIMIZE_RESOURCE_MEMORY_REQUIREMENTS_MODIFIER_H
