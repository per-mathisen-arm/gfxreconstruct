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

#include "vulkan_shader_replacement_modifier.h"

#include "encode/parameter_encoder.h"
#include "encode/struct_pointer_encoder.h"
#include "graphics/vulkan_struct_get_pnext.h"

GFXRECON_BEGIN_NAMESPACE(gfxrecon)
GFXRECON_BEGIN_NAMESPACE(decode)

VulkanShaderReplacementModifier::VulkanShaderReplacementModifier(const std::string& shader_dir) :
    shader_dir_(shader_dir), shaders_()
{}

void VulkanShaderReplacementModifier::Process_vkCreateShaderModule(
    const ApiCallInfo&                                      call_info,
    VkResult                                                returnValue,
    format::HandleId                                        device,
    StructPointerDecoder<Decoded_VkShaderModuleCreateInfo>* pCreateInfo,
    StructPointerDecoder<Decoded_VkAllocationCallbacks>*    pAllocator,
    HandlePointerDecoder<VkShaderModule>*                   pShaderModule)
{
    if (returnValue != VK_SUCCESS)
    {
        return;
    }

    GFXRECON_ASSERT(pCreateInfo != nullptr && pAllocator != nullptr && pShaderModule != nullptr);

    VkShaderModuleCreateInfo* create_info = pCreateInfo->GetPointer();
    GFXRECON_ASSERT(create_info != nullptr);

    const format::HandleId handle_id = *pShaderModule->GetPointer();
    GFXRECON_ASSERT(handle_id != format::kNullHandleId);

    const std::string file_name = "sh" + std::to_string(handle_id);

    auto it = shaders_.find(file_name);

    // First pass, load the shader files
    if (!IsModificationPass())
    {
        GFXRECON_ASSERT(it == shaders_.end());
        TryLoadShader(file_name);
    }

    // Second pass, replace the shaders if replacement exists
    else if (it != shaders_.end())
    {
        const std::vector<char>& code_buffer = it->second;

        create_info->codeSize = code_buffer.size();
        create_info->pCode    = reinterpret_cast<const uint32_t*>(code_buffer.data());

        NewCallData* new_call = CreatePreCall();
        new_call->type        = NewCallDataType::ApiCall;
        new_call->call_id     = format::ApiCallId::ApiCall_vkCreateShaderModule;
        new_call->thread_id   = call_info.thread_id;

        encode::ParameterEncoder encoder(&new_call->parameter_buffer);

        encoder.EncodeHandleIdValue(device);
        encode::EncodeStructPtr(&encoder, create_info);
        encode::EncodeStructPtr(&encoder, pAllocator->GetPointer());
        encoder.EncodeHandleIdPtr(&handle_id);
        encoder.EncodeEnumValue(returnValue);

        SetDeleteCurrentCall();
    }
}

void VulkanShaderReplacementModifier::Process_vkCreateShadersEXT(
    const ApiCallInfo&                                   call_info,
    VkResult                                             returnValue,
    format::HandleId                                     device,
    uint32_t                                             createInfoCount,
    StructPointerDecoder<Decoded_VkShaderCreateInfoEXT>* pCreateInfos,
    StructPointerDecoder<Decoded_VkAllocationCallbacks>* pAllocator,
    HandlePointerDecoder<VkShaderEXT>*                   pShaders)
{
    if (returnValue != VK_SUCCESS)
    {
        return;
    }

    GFXRECON_ASSERT(pCreateInfos != nullptr && pAllocator != nullptr && pShaders != nullptr);

    VkShaderCreateInfoEXT* create_infos = pCreateInfos->GetPointer();
    GFXRECON_ASSERT(create_infos != nullptr);

    const format::HandleId* handle_ids = pShaders->GetPointer();

    bool replace_call = false;
    for (uint32_t i = 0; i < createInfoCount; ++i)
    {
        const std::string file_name = "sh" + std::to_string(handle_ids[i]);

        auto it = shaders_.find(file_name);

        // First pass, load the shader files
        if (!IsModificationPass())
        {
            GFXRECON_ASSERT(it == shaders_.end());
            TryLoadShader(file_name);
        }

        // Second pass, replace the shaders if replacement exists
        else if (it != shaders_.end())
        {
            const std::vector<char>& code_buffer = it->second;

            create_infos[i].codeSize = code_buffer.size();
            create_infos[i].pCode    = code_buffer.data();

            replace_call = true;
        }
    }

    if (!replace_call)
    {
        return;
    }

    NewCallData* new_call = CreatePreCall();
    new_call->type        = NewCallDataType::ApiCall;
    new_call->call_id     = format::ApiCallId::ApiCall_vkCreateShadersEXT;
    new_call->thread_id   = call_info.thread_id;

    encode::ParameterEncoder encoder(&new_call->parameter_buffer);

    encoder.EncodeHandleIdValue(device);
    encoder.EncodeUInt32Value(createInfoCount);
    encode::EncodeStructArray(&encoder, create_infos, createInfoCount);
    encode::EncodeStructPtr(&encoder, pAllocator->GetPointer());
    encoder.EncodeHandleIdArray(handle_ids, createInfoCount);
    encoder.EncodeEnumValue(returnValue);

    SetDeleteCurrentCall();
}

void VulkanShaderReplacementModifier::Process_vkCreateGraphicsPipelines(
    const ApiCallInfo&                                          call_info,
    VkResult                                                    returnValue,
    format::HandleId                                            device,
    format::HandleId                                            pipelineCache,
    uint32_t                                                    createInfoCount,
    StructPointerDecoder<Decoded_VkGraphicsPipelineCreateInfo>* pCreateInfos,
    StructPointerDecoder<Decoded_VkAllocationCallbacks>*        pAllocator,
    HandlePointerDecoder<VkPipeline>*                           pPipelines)
{
    if (returnValue != VK_SUCCESS)
    {
        return;
    }

    GFXRECON_ASSERT(pCreateInfos != nullptr && pAllocator != nullptr && pPipelines != nullptr);

    VkGraphicsPipelineCreateInfo* pipeline_create_infos = pCreateInfos->GetPointer();
    GFXRECON_ASSERT(pipeline_create_infos != nullptr);

    const format::HandleId* pipeline_ids = pPipelines->GetPointer();

    bool replace_call = false;
    for (uint32_t i = 0; i < createInfoCount; ++i)
    {
        const format::HandleId&       pipeline_id          = pipeline_ids[i];
        VkGraphicsPipelineCreateInfo& pipeline_create_info = pipeline_create_infos[i];

        for (uint32_t j = 0; j < pipeline_create_info.stageCount; ++j)
        {
            VkPipelineShaderStageCreateInfo* stage_create_info =
                const_cast<VkPipelineShaderStageCreateInfo*>(pipeline_create_info.pStages + j);

            if (TryLoadOrReplaceShader(pipeline_id, stage_create_info) && IsModificationPass())
            {
                replace_call = true;
            }
        }
    }

    if (!replace_call)
    {
        return;
    }

    NewCallData* new_call = CreatePreCall();
    new_call->type        = NewCallDataType::ApiCall;
    new_call->call_id     = format::ApiCallId::ApiCall_vkCreateGraphicsPipelines;
    new_call->thread_id   = call_info.thread_id;

    encode::ParameterEncoder encoder(&new_call->parameter_buffer);

    encoder.EncodeHandleIdValue(device);
    encoder.EncodeHandleIdValue(pipelineCache);
    encoder.EncodeUInt32Value(createInfoCount);
    encode::EncodeStructArray(&encoder, pipeline_create_infos, createInfoCount);
    encode::EncodeStructPtr(&encoder, pAllocator->GetPointer());
    encoder.EncodeHandleIdArray(pipeline_ids, createInfoCount);
    encoder.EncodeEnumValue(returnValue);

    SetDeleteCurrentCall();
}

void VulkanShaderReplacementModifier::Process_vkCreateComputePipelines(
    const ApiCallInfo&                                         call_info,
    VkResult                                                   returnValue,
    format::HandleId                                           device,
    format::HandleId                                           pipelineCache,
    uint32_t                                                   createInfoCount,
    StructPointerDecoder<Decoded_VkComputePipelineCreateInfo>* pCreateInfos,
    StructPointerDecoder<Decoded_VkAllocationCallbacks>*       pAllocator,
    HandlePointerDecoder<VkPipeline>*                          pPipelines)
{
    if (returnValue != VK_SUCCESS)
    {
        return;
    }

    GFXRECON_ASSERT(pCreateInfos != nullptr && pAllocator != nullptr && pPipelines != nullptr);

    VkComputePipelineCreateInfo* pipeline_create_infos = pCreateInfos->GetPointer();
    GFXRECON_ASSERT(pipeline_create_infos != nullptr);

    const format::HandleId* pipeline_ids = pPipelines->GetPointer();

    bool replace_call = false;
    for (uint32_t i = 0; i < createInfoCount; ++i)
    {
        const format::HandleId&          pipeline_id       = pipeline_ids[i];
        VkPipelineShaderStageCreateInfo* stage_create_info = &pipeline_create_infos[i].stage;

        if (TryLoadOrReplaceShader(pipeline_id, stage_create_info) && IsModificationPass())
        {
            replace_call = true;
        }
    }

    if (!replace_call)
    {
        return;
    }

    NewCallData* new_call = CreatePreCall();
    new_call->type        = NewCallDataType::ApiCall;
    new_call->call_id     = format::ApiCallId::ApiCall_vkCreateComputePipelines;
    new_call->thread_id   = call_info.thread_id;

    encode::ParameterEncoder encoder(&new_call->parameter_buffer);

    encoder.EncodeHandleIdValue(device);
    encoder.EncodeHandleIdValue(pipelineCache);
    encoder.EncodeUInt32Value(createInfoCount);
    encode::EncodeStructArray(&encoder, pipeline_create_infos, createInfoCount);
    encode::EncodeStructPtr(&encoder, pAllocator->GetPointer());
    encoder.EncodeHandleIdArray(pipeline_ids, createInfoCount);
    encoder.EncodeEnumValue(returnValue);

    SetDeleteCurrentCall();
}

void VulkanShaderReplacementModifier::Process_vkCreateRayTracingPipelinesKHR(
    const ApiCallInfo&                                               call_info,
    VkResult                                                         returnValue,
    format::HandleId                                                 device,
    format::HandleId                                                 deferredOperation,
    format::HandleId                                                 pipelineCache,
    uint32_t                                                         createInfoCount,
    StructPointerDecoder<Decoded_VkRayTracingPipelineCreateInfoKHR>* pCreateInfos,
    StructPointerDecoder<Decoded_VkAllocationCallbacks>*             pAllocator,
    HandlePointerDecoder<VkPipeline>*                                pPipelines)
{
    if (returnValue != VK_SUCCESS)
    {
        return;
    }

    GFXRECON_ASSERT(pCreateInfos != nullptr && pAllocator != nullptr && pPipelines != nullptr);

    VkRayTracingPipelineCreateInfoKHR* pipeline_create_infos = pCreateInfos->GetPointer();
    GFXRECON_ASSERT(pipeline_create_infos != nullptr);

    const format::HandleId* pipeline_ids = pPipelines->GetPointer();

    bool replace_call = false;
    for (uint32_t i = 0; i < createInfoCount; ++i)
    {
        const format::HandleId&            pipeline_id          = pipeline_ids[i];
        VkRayTracingPipelineCreateInfoKHR& pipeline_create_info = pipeline_create_infos[i];

        for (uint32_t j = 0; j < pipeline_create_info.stageCount; ++j)
        {
            VkPipelineShaderStageCreateInfo* stage_create_info =
                const_cast<VkPipelineShaderStageCreateInfo*>(pipeline_create_info.pStages + j);

            if (TryLoadOrReplaceShader(pipeline_id, stage_create_info) && IsModificationPass())
            {
                replace_call = true;
            }
        }
    }

    if (!replace_call)
    {
        return;
    }

    NewCallData* new_call = CreatePreCall();
    new_call->type        = NewCallDataType::ApiCall;
    new_call->call_id     = format::ApiCallId::ApiCall_vkCreateRayTracingPipelinesKHR;
    new_call->thread_id   = call_info.thread_id;

    encode::ParameterEncoder encoder(&new_call->parameter_buffer);

    encoder.EncodeHandleIdValue(device);
    encoder.EncodeHandleIdValue(deferredOperation);
    encoder.EncodeHandleIdValue(pipelineCache);
    encoder.EncodeUInt32Value(createInfoCount);
    encode::EncodeStructArray(&encoder, pipeline_create_infos, createInfoCount);
    encode::EncodeStructPtr(&encoder, pAllocator->GetPointer());
    encoder.EncodeHandleIdArray(pipeline_ids, createInfoCount);
    encoder.EncodeEnumValue(returnValue);

    SetDeleteCurrentCall();
}

void VulkanShaderReplacementModifier::Process_vkCreateRayTracingPipelinesNV(
    const ApiCallInfo&                                              call_info,
    VkResult                                                        returnValue,
    format::HandleId                                                device,
    format::HandleId                                                pipelineCache,
    uint32_t                                                        createInfoCount,
    StructPointerDecoder<Decoded_VkRayTracingPipelineCreateInfoNV>* pCreateInfos,
    StructPointerDecoder<Decoded_VkAllocationCallbacks>*            pAllocator,
    HandlePointerDecoder<VkPipeline>*                               pPipelines)
{
    if (returnValue != VK_SUCCESS)
    {
        return;
    }

    GFXRECON_ASSERT(pCreateInfos != nullptr && pAllocator != nullptr && pPipelines != nullptr);

    VkRayTracingPipelineCreateInfoNV* pipeline_create_infos = pCreateInfos->GetPointer();
    GFXRECON_ASSERT(pipeline_create_infos != nullptr);

    const format::HandleId* pipeline_ids = pPipelines->GetPointer();

    bool replace_call = false;
    for (uint32_t i = 0; i < createInfoCount; ++i)
    {
        const format::HandleId&           pipeline_id          = pipeline_ids[i];
        VkRayTracingPipelineCreateInfoNV& pipeline_create_info = pipeline_create_infos[i];

        for (uint32_t j = 0; j < pipeline_create_info.stageCount; ++j)
        {
            VkPipelineShaderStageCreateInfo* stage_create_info =
                const_cast<VkPipelineShaderStageCreateInfo*>(pipeline_create_info.pStages + j);

            if (TryLoadOrReplaceShader(pipeline_id, stage_create_info) && IsModificationPass())
            {
                replace_call = true;
            }
        }
    }

    if (!replace_call)
    {
        return;
    }

    NewCallData* new_call = CreatePreCall();
    new_call->type        = NewCallDataType::ApiCall;
    new_call->call_id     = format::ApiCallId::ApiCall_vkCreateRayTracingPipelinesNV;
    new_call->thread_id   = call_info.thread_id;

    encode::ParameterEncoder encoder(&new_call->parameter_buffer);

    encoder.EncodeHandleIdValue(device);
    encoder.EncodeHandleIdValue(pipelineCache);
    encoder.EncodeUInt32Value(createInfoCount);
    encode::EncodeStructArray(&encoder, pipeline_create_infos, createInfoCount);
    encode::EncodeStructPtr(&encoder, pAllocator->GetPointer());
    encoder.EncodeHandleIdArray(pipeline_ids, createInfoCount);
    encoder.EncodeEnumValue(returnValue);

    SetDeleteCurrentCall();
}

void VulkanShaderReplacementModifier::Process_vkCreateDataGraphPipelinesARM(
    const ApiCallInfo&                                              call_info,
    VkResult                                                        returnValue,
    format::HandleId                                                device,
    format::HandleId                                                deferredOperation,
    format::HandleId                                                pipelineCache,
    uint32_t                                                        createInfoCount,
    StructPointerDecoder<Decoded_VkDataGraphPipelineCreateInfoARM>* pCreateInfos,
    StructPointerDecoder<Decoded_VkAllocationCallbacks>*            pAllocator,
    HandlePointerDecoder<VkPipeline>*                               pPipelines)
{
    if (returnValue != VK_SUCCESS)
    {
        return;
    }

    GFXRECON_ASSERT(pCreateInfos != nullptr && pAllocator != nullptr && pPipelines != nullptr);

    VkDataGraphPipelineCreateInfoARM* pipeline_create_infos = pCreateInfos->GetPointer();
    GFXRECON_ASSERT(pipeline_create_infos != nullptr);

    const format::HandleId* pipeline_ids = pPipelines->GetPointer();

    bool replace_call = false;
    for (uint32_t i = 0; i < createInfoCount; ++i)
    {
        const format::HandleId&           pipeline_id          = pipeline_ids[i];
        VkDataGraphPipelineCreateInfoARM& pipeline_create_info = pipeline_create_infos[i];

        const std::string file_name = "sh" + std::to_string(pipeline_id);

        auto it = shaders_.find(file_name);

        // First pass, load the shader files
        if (!IsModificationPass())
        {
            GFXRECON_ASSERT(it == shaders_.end());
            TryLoadShader(file_name);
        }

        // Second pass, replace the shaders if replacement exists
        else if (it != shaders_.end())
        {
            const std::vector<char>& code_buffer = it->second;

            VkShaderModuleCreateInfo* sm_create_info =
                graphics::vulkan_struct_get_pnext<VkShaderModuleCreateInfo>(&pipeline_create_info);

            // If a replacement shader with the exact same id exists, then the struct MUST be in the pNext chain
            GFXRECON_ASSERT(sm_create_info != nullptr);

            sm_create_info->codeSize = code_buffer.size();
            sm_create_info->pCode    = reinterpret_cast<const uint32_t*>(code_buffer.data());

            replace_call = true;
        }
    }

    if (!replace_call)
    {
        return;
    }

    NewCallData* new_call = CreatePreCall();
    new_call->type        = NewCallDataType::ApiCall;
    new_call->call_id     = format::ApiCallId::ApiCall_vkCreateDataGraphPipelinesARM;
    new_call->thread_id   = call_info.thread_id;

    encode::ParameterEncoder encoder(&new_call->parameter_buffer);

    encoder.EncodeHandleIdValue(device);
    encoder.EncodeHandleIdValue(deferredOperation);
    encoder.EncodeHandleIdValue(pipelineCache);
    encoder.EncodeUInt32Value(createInfoCount);
    encode::EncodeStructArray(&encoder, pipeline_create_infos, createInfoCount);
    encode::EncodeStructPtr(&encoder, pAllocator->GetPointer());
    encoder.EncodeHandleIdArray(pipeline_ids, createInfoCount);
    encoder.EncodeEnumValue(returnValue);

    SetDeleteCurrentCall();
}

bool VulkanShaderReplacementModifier::TryLoadShader(const std::string& file_name)
{
    const std::string file_path = util::filepath::Join(shader_dir_, file_name);

    FILE* fp = nullptr;
    if (util::platform::FileOpen(&fp, file_path.c_str(), "rb") != 0)
    {
        return false;
    }

    GFXRECON_LOG_INFO("Replacement shader found: %s", file_path.c_str());

    std::vector<char>& code_buffer = shaders_[file_name];

    util::platform::FileSeek(fp, 0L, util::platform::FileSeekEnd);
    code_buffer.resize(util::platform::FileTell(fp));
    util::platform::FileSeek(fp, 0L, util::platform::FileSeekSet);
    util::platform::FileRead(code_buffer.data(), code_buffer.size(), fp);
    util::platform::FileClose(fp);

    return true;
}

bool VulkanShaderReplacementModifier::TryLoadOrReplaceShader(format::HandleId                 pipeline_id,
                                                             VkPipelineShaderStageCreateInfo* stage_create_info)
{
    VkShaderModuleCreateInfo* sm_create_info =
        graphics::vulkan_struct_get_pnext<VkShaderModuleCreateInfo>(stage_create_info);

    if (sm_create_info == nullptr)
    {
        return false;
    }

    const std::string file_name = "sh" + std::to_string(pipeline_id) + "_" + std::to_string(stage_create_info->stage);

    auto it = shaders_.find(file_name);

    // First pass, load the shader files
    if (!IsModificationPass())
    {
        GFXRECON_ASSERT(it == shaders_.end());
        return TryLoadShader(file_name);
    }

    // Second pass, replace the shaders if replacement exists
    else if (it != shaders_.end())
    {
        const std::vector<char>& code_buffer = it->second;

        sm_create_info->codeSize = code_buffer.size();
        sm_create_info->pCode    = reinterpret_cast<const uint32_t*>(code_buffer.data());

        return true;
    }

    return false;
}

GFXRECON_END_NAMESPACE(decode)
GFXRECON_END_NAMESPACE(gfxrecon)
