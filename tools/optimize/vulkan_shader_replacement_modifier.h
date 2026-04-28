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

#ifndef GFXRECON_DECODE_VULKAN_SHADER_REPLACEMENT_MODIFIER_H
#define GFXRECON_DECODE_VULKAN_SHADER_REPLACEMENT_MODIFIER_H

#include <string>

#include "util/vulkan_modifier_base.h"

#include "vulkan/vulkan.h"

GFXRECON_BEGIN_NAMESPACE(gfxrecon)
GFXRECON_BEGIN_NAMESPACE(decode)

class VulkanShaderReplacementModifier : public util::VulkanModifierBase
{
  public:
    VulkanShaderReplacementModifier(const std::string& shader_dir);

    bool CanOptimize() override { return !shaders_.empty(); };

    virtual void Process_vkCreateShaderModule(const ApiCallInfo&                                      call_info,
                                              VkResult                                                returnValue,
                                              format::HandleId                                        device,
                                              StructPointerDecoder<Decoded_VkShaderModuleCreateInfo>* pCreateInfo,
                                              StructPointerDecoder<Decoded_VkAllocationCallbacks>*    pAllocator,
                                              HandlePointerDecoder<VkShaderModule>* pShaderModule) override;

    virtual void Process_vkCreateShadersEXT(const ApiCallInfo&                                   call_info,
                                            VkResult                                             returnValue,
                                            format::HandleId                                     device,
                                            uint32_t                                             createInfoCount,
                                            StructPointerDecoder<Decoded_VkShaderCreateInfoEXT>* pCreateInfos,
                                            StructPointerDecoder<Decoded_VkAllocationCallbacks>* pAllocator,
                                            HandlePointerDecoder<VkShaderEXT>*                   pShaders) override;

    virtual void
    Process_vkCreateGraphicsPipelines(const ApiCallInfo&                                          call_info,
                                      VkResult                                                    returnValue,
                                      format::HandleId                                            device,
                                      format::HandleId                                            pipelineCache,
                                      uint32_t                                                    createInfoCount,
                                      StructPointerDecoder<Decoded_VkGraphicsPipelineCreateInfo>* pCreateInfos,
                                      StructPointerDecoder<Decoded_VkAllocationCallbacks>*        pAllocator,
                                      HandlePointerDecoder<VkPipeline>*                           pPipelines) override;

    virtual void
    Process_vkCreateComputePipelines(const ApiCallInfo&                                         call_info,
                                     VkResult                                                   returnValue,
                                     format::HandleId                                           device,
                                     format::HandleId                                           pipelineCache,
                                     uint32_t                                                   createInfoCount,
                                     StructPointerDecoder<Decoded_VkComputePipelineCreateInfo>* pCreateInfos,
                                     StructPointerDecoder<Decoded_VkAllocationCallbacks>*       pAllocator,
                                     HandlePointerDecoder<VkPipeline>*                          pPipelines) override;

    virtual void Process_vkCreateRayTracingPipelinesKHR(
        const ApiCallInfo&                                               call_info,
        VkResult                                                         returnValue,
        format::HandleId                                                 device,
        format::HandleId                                                 deferredOperation,
        format::HandleId                                                 pipelineCache,
        uint32_t                                                         createInfoCount,
        StructPointerDecoder<Decoded_VkRayTracingPipelineCreateInfoKHR>* pCreateInfos,
        StructPointerDecoder<Decoded_VkAllocationCallbacks>*             pAllocator,
        HandlePointerDecoder<VkPipeline>*                                pPipelines) override;

    virtual void
    Process_vkCreateRayTracingPipelinesNV(const ApiCallInfo& call_info,
                                          VkResult           returnValue,
                                          format::HandleId   device,
                                          format::HandleId   pipelineCache,
                                          uint32_t           createInfoCount,
                                          StructPointerDecoder<Decoded_VkRayTracingPipelineCreateInfoNV>* pCreateInfos,
                                          StructPointerDecoder<Decoded_VkAllocationCallbacks>*            pAllocator,
                                          HandlePointerDecoder<VkPipeline>* pPipelines) override;

    virtual void
    Process_vkCreateDataGraphPipelinesARM(const ApiCallInfo& call_info,
                                          VkResult           returnValue,
                                          format::HandleId   device,
                                          format::HandleId   deferredOperation,
                                          format::HandleId   pipelineCache,
                                          uint32_t           createInfoCount,
                                          StructPointerDecoder<Decoded_VkDataGraphPipelineCreateInfoARM>* pCreateInfos,
                                          StructPointerDecoder<Decoded_VkAllocationCallbacks>*            pAllocator,
                                          HandlePointerDecoder<VkPipeline>* pPipelines) override;

  private:
    bool TryLoadShader(const std::string& file_name);
    bool TryLoadOrReplaceShader(format::HandleId pipeline_id, VkPipelineShaderStageCreateInfo* stage_create_info);

    std::string                                        shader_dir_;
    std::unordered_map<std::string, std::vector<char>> shaders_;
};

GFXRECON_END_NAMESPACE(decode)
GFXRECON_END_NAMESPACE(gfxrecon)

#endif // GFXRECON_DECODE_VULKAN_SHADER_REPLACEMENT_MODIFIER_H
