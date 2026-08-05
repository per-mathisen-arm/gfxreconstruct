/*
** Copyright (c) 2024 LunarG, Inc.
** Copyright (c) 2024 Arm Limited and/or its affiliates <open-source-office@arm.com>
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

#ifndef GFXRECON_DECODE_VULKAN_FEATURE_TRACKER_CONSUMER_BASE_H
#define GFXRECON_DECODE_VULKAN_FEATURE_TRACKER_CONSUMER_BASE_H

#include "decode/referenced_resource_table.h"
#include "generated/generated_vulkan_consumer.h"
#include "util/defines.h"
#include "util/memory_output_stream.h"
#include "encode/parameter_buffer.h"
#include "util/vulkan_modifier_base.h"

#include "vulkan/vulkan.h"

#include <functional>
#include <limits>
#include <unordered_map>
#include <unordered_set>

GFXRECON_BEGIN_NAMESPACE(gfxrecon)
GFXRECON_BEGIN_NAMESPACE(decode)

class VulkanFeatureTrackerConsumerBase : public util::VulkanModifierBase
{
  public:
    VulkanFeatureTrackerConsumerBase();

    bool CanOptimize() override;

    void PrintAllFeatures();

    void Process_vkCreateInstance(const ApiCallInfo& call_info, args::CreateInstance& args) override;

    void Process_vkCreateDevice(const ApiCallInfo& call_info, args::CreateDevice& args) override;

    void Process_vkCmdBeginRendering(const ApiCallInfo& call_info, args::CmdBeginRendering& args) override;

    void Process_vkCmdBeginRenderingKHR(const ApiCallInfo& call_info, args::CmdBeginRenderingKHR& args) override;

    void Process_vkCreateBuffer(const ApiCallInfo& call_info, args::CreateBuffer& args) override;

    void Process_vkCreateImage(const ApiCallInfo& call_info, args::CreateImage& args) override;

    void Process_vkCreateImageView(const ApiCallInfo& call_info, args::CreateImageView& args) override;

    void Process_vkCreateGraphicsPipelines(const ApiCallInfo& call_info, args::CreateGraphicsPipelines& args) override;

    void Process_vkCreateComputePipelines(const ApiCallInfo& call_info, args::CreateComputePipelines& args) override;

    void Process_vkCreateRayTracingPipelinesKHR(const ApiCallInfo&                  call_info,
                                                args::CreateRayTracingPipelinesKHR& args) override;

    void Process_vkCmdDrawIndirect(const ApiCallInfo& call_info, args::CmdDrawIndirect& args) override;

    void Process_vkCmdDrawIndirectCount(const ApiCallInfo& call_info, args::CmdDrawIndirectCount& args) override;

    void Process_vkCmdDrawIndexedIndirect(const ApiCallInfo& call_info, args::CmdDrawIndexedIndirect& args) override;

    void Process_vkCmdDrawIndexedIndirectCount(const ApiCallInfo&                 call_info,
                                               args::CmdDrawIndexedIndirectCount& args) override;

    void Process_vkBeginCommandBuffer(const ApiCallInfo& call_info, args::BeginCommandBuffer& args) override;

    void Process_vkCmdSetPolygonModeEXT(const ApiCallInfo& call_info, args::CmdSetPolygonModeEXT& args) override;

    void Process_vkCmdSetViewport(const ApiCallInfo& call_info, args::CmdSetViewport& args) override;

    void Process_vkCmdSetScissor(const ApiCallInfo& call_info, args::CmdSetScissor& args) override;

    void Process_vkCmdSetExclusiveScissorNV(const ApiCallInfo&              call_info,
                                            args::CmdSetExclusiveScissorNV& args) override;

    void Process_vkCreateSampler(const ApiCallInfo& call_info, args::CreateSampler& args) override;

    void Process_vkCreateQueryPool(const ApiCallInfo& call_info, args::CreateQueryPool& args) override;

    void Process_vkResetQueryPool(const ApiCallInfo& call_info, args::ResetQueryPool& args) override;

    void Process_vkCreateSwapchainKHR(const ApiCallInfo& call_info, args::CreateSwapchainKHR& args) override;

    void Process_vkCreateSharedSwapchainsKHR(const ApiCallInfo&               call_info,
                                             args::CreateSharedSwapchainsKHR& args) override;

    void Process_vkCmdBindIndexBuffer(const ApiCallInfo& call_info, args::CmdBindIndexBuffer& args) override;

    void Process_vkCmdBindIndexBuffer2(const ApiCallInfo& call_info, args::CmdBindIndexBuffer2& args) override;

    void Process_vkCmdBindIndexBuffer2KHR(const ApiCallInfo& call_info, args::CmdBindIndexBuffer2KHR& args) override;

    void Process_vkCmdSetDepthBias(const ApiCallInfo& call_info, args::CmdSetDepthBias& args) override;

    void Process_vkCmdSetLineWidth(const ApiCallInfo& call_info, args::CmdSetLineWidth& args) override;

    void Process_vkCmdBeginQuery(const ApiCallInfo& call_info, args::CmdBeginQuery& args) override;

    void Process_vkCreateShaderModule(const ApiCallInfo& call_info, args::CreateShaderModule& args) override;

    void Process_vkCreateSemaphore(const ApiCallInfo& call_info, args::CreateSemaphore& args) override;

    void Process_vkGetBufferDeviceAddress(const ApiCallInfo& call_info, args::GetBufferDeviceAddress& args) override;

    void Process_vkGetBufferDeviceAddressEXT(const ApiCallInfo&               call_info,
                                             args::GetBufferDeviceAddressEXT& args) override;

    void Process_vkGetBufferDeviceAddressKHR(const ApiCallInfo&               call_info,
                                             args::GetBufferDeviceAddressKHR& args) override;

    void Process_vkGetBufferOpaqueCaptureAddress(const ApiCallInfo&                   call_info,
                                                 args::GetBufferOpaqueCaptureAddress& args) override;

  private:
    void parse_SPIRV(const uint32_t* code, uint32_t code_size);

    void checkSwapchainColorspaceEXT(VkColorSpaceKHR s);

    void Process_VkPipelineShaderStageCreateInfo(const VkPipelineShaderStageCreateInfo* info);

    bool ProcessInstanceExtensions();
    bool ProcessDeviceExtensions();

    bool ProcessCore10Features();
    bool ProcessCore11Features();
    bool ProcessCore12Features();
    bool ProcessCore13Features();
    bool ProcessCore14Features();

  private:
    // capture_corexx_ holds the data in the order of passing (first encounter is the first element)
    // output_corexx_ holds the data in the reverse order of passing (first encounter is the last element)
    // same logic for capture_xxxxx_extensions_vector_ and output_xxxxx_extensions_vector_

    std::vector<std::string>              core10_members_as_strings_{};
    VkPhysicalDeviceFeatures              core10_{};
    std::vector<VkPhysicalDeviceFeatures> capture_core10_{};
    std::vector<VkPhysicalDeviceFeatures> output_core10_{};

    std::vector<std::string>                      core11_members_as_strings_{};
    VkPhysicalDeviceVulkan11Features              core11_{};
    std::vector<VkPhysicalDeviceVulkan11Features> capture_core11_{};
    std::vector<VkPhysicalDeviceVulkan11Features> output_core11_{};

    std::vector<std::string>                      core12_members_as_strings_{};
    VkPhysicalDeviceVulkan12Features              core12_{};
    std::vector<VkPhysicalDeviceVulkan12Features> capture_core12_{};
    std::vector<VkPhysicalDeviceVulkan12Features> output_core12_{};

    std::vector<std::string>                      core13_members_as_strings_{};
    VkPhysicalDeviceVulkan13Features              core13_{};
    std::vector<VkPhysicalDeviceVulkan13Features> capture_core13_{};
    std::vector<VkPhysicalDeviceVulkan13Features> output_core13_{};

    std::vector<std::string>                      core14_members_as_strings_{};
    VkPhysicalDeviceVulkan14Features              core14_{};
    std::vector<VkPhysicalDeviceVulkan14Features> capture_core14_{};
    std::vector<VkPhysicalDeviceVulkan14Features> output_core14_{};

    std::vector<std::vector<std::string>> capture_instance_extensions_vector_{};
    std::vector<std::vector<std::string>> capture_device_extensions_vector_{};

    std::unordered_map<std::string, VkBool32> supported_instance_extensions_map_{};
    std::unordered_map<std::string, VkBool32> supported_device_extensions_map_{};

    std::vector<std::vector<std::string>> output_instance_extensions_vector_{};
    std::vector<std::vector<std::string>> output_device_extensions_vector_{};

    std::string consumer_output_log_{};
};

GFXRECON_END_NAMESPACE(decode)
GFXRECON_END_NAMESPACE(gfxrecon)

#endif // GFXRECON_DECODE_VULKAN_FEATURE_TRACKER_CONSUMER_BASE_H
