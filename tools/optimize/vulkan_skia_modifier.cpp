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

#include "generated/generated_vulkan_skiavk_modifier.h"

#include "graphics/vulkan_struct_get_pnext.h"
#include "graphics/vulkan_util.h"
#include "util/logging.h"

#include <cassert>
#include <cstring>
#include <stdexcept>

#include "generated/generated_vulkan_api_call_encoders.h"

#include "encode/custom_vulkan_encoder_commands.h"
#include "encode/custom_vulkan_array_size_2d.h"
#include "encode/parameter_encoder.h"
#include "encode/struct_pointer_encoder.h"
#include "encode/vulkan_capture_manager.h"
#include "encode/vulkan_handle_wrapper_util.h"
#include "encode/vulkan_handle_wrappers.h"
#include "format/api_call_id.h"
#include "generated/generated_vulkan_command_buffer_util.h"
#include "generated/generated_vulkan_struct_handle_wrappers.h"
#include "util/defines.h"

GFXRECON_BEGIN_NAMESPACE(gfxrecon)
GFXRECON_BEGIN_NAMESPACE(decode)

std::vector<std::string> VulkanSkiaModifier::app_name_array = {};

bool VulkanSkiaModifier::SubmitHasFrameEndMarker(uint32_t                                    submit_count,
                                                 StructPointerDecoder<Decoded_VkSubmitInfo>* pSubmits) const
{
    if ((submit_count == 0) || (pSubmits == nullptr))
    {
        return false;
    }

    const VkSubmitInfo*         submits      = pSubmits->GetPointer();
    const Decoded_VkSubmitInfo* submit_metas = pSubmits->GetMetaStructPointer();
    if ((submits == nullptr) || (submit_metas == nullptr))
    {
        return false;
    }

    for (uint32_t i = 0; i < submit_count; ++i)
    {
        auto* frame_boundary = graphics::vulkan_struct_get_pnext<VkFrameBoundaryEXT>(
            reinterpret_cast<const VkBaseInStructure*>(submits + i));
        if ((frame_boundary != nullptr) && ((frame_boundary->flags & VK_FRAME_BOUNDARY_FRAME_END_BIT_EXT) != 0))
        {
            return true;
        }

        auto* command_buffers = submit_metas[i].pCommandBuffers.GetPointer();
        for (uint32_t j = 0; j < submits[i].commandBufferCount; ++j)
        {
            if ((command_buffers != nullptr) && frame_boundary_command_buffers_.contains(command_buffers[j]))
            {
                return true;
            }
        }
    }

    return false;
}

bool VulkanSkiaModifier::Submit2HasFrameEndMarker(uint32_t                                     submit_count,
                                                  StructPointerDecoder<Decoded_VkSubmitInfo2>* pSubmits) const
{
    if ((submit_count == 0) || (pSubmits == nullptr))
    {
        return false;
    }

    const VkSubmitInfo2*         submits      = pSubmits->GetPointer();
    const Decoded_VkSubmitInfo2* submit_metas = pSubmits->GetMetaStructPointer();
    if ((submits == nullptr) || (submit_metas == nullptr))
    {
        return false;
    }

    for (uint32_t i = 0; i < submit_count; ++i)
    {
        auto* frame_boundary = graphics::vulkan_struct_get_pnext<VkFrameBoundaryEXT>(
            reinterpret_cast<const VkBaseInStructure*>(submits + i));
        if ((frame_boundary != nullptr) && ((frame_boundary->flags & VK_FRAME_BOUNDARY_FRAME_END_BIT_EXT) != 0))
        {
            return true;
        }

        if (submit_metas[i].pCommandBufferInfos == nullptr)
        {
            continue;
        }

        auto* command_buffer_infos = submit_metas[i].pCommandBufferInfos->GetMetaStructPointer();
        for (uint32_t j = 0; j < submits[i].commandBufferInfoCount; ++j)
        {
            if ((command_buffer_infos != nullptr) &&
                frame_boundary_command_buffers_.contains(command_buffer_infos[j].commandBuffer))
            {
                return true;
            }
        }
    }

    return false;
}

bool VulkanSkiaModifier::ContainsVrFrameDelimiter(const char* label) const
{
    return (label != nullptr) && (std::strstr(label, graphics::kVulkanVrFrameDelimiterString) != nullptr);
}

void VulkanSkiaModifier::AppendFrameEndMarkerForCurrentBlock()
{
    if (!frame_end_marker_blocks_to_insert_.contains(block_index_))
    {
        return;
    }

    auto* frame_marker         = CreatePostCall();
    frame_marker->type         = util::CallModifierBase::NewCallDataType::FrameMarkerCall;
    frame_marker->frame_number = next_output_frame_number_++;

    frame_end_marker_blocks_to_insert_.erase(block_index_);
}

void VulkanSkiaModifier::ProcessStateEndMarker(uint64_t frame_number)
{
    if (!IsModificationPass() && !has_output_frame_number_base_)
    {
        next_output_frame_number_     = frame_number;
        has_output_frame_number_base_ = true;
    }
}

void VulkanSkiaModifier::ProcessFrameEndMarker(uint64_t frame_number)
{
    GFXRECON_UNREFERENCED_PARAMETER(frame_number);
    SetDeleteCurrentCall();
}

void VulkanSkiaModifier::Process_vkQueuePresentKHR(const ApiCallInfo& call_info, args::QueuePresentKHR& args)
{
    if (IsModificationPass())
    {
        AppendFrameEndMarkerForCurrentBlock();
        return;
    }

    frame_end_marker_blocks_to_insert_.insert(block_index_);
}

void VulkanSkiaModifier::Process_vkQueueSubmit(const ApiCallInfo& call_info, args::QueueSubmit& args)
{
    if (IsModificationPass())
    {
        AppendFrameEndMarkerForCurrentBlock();
        return;
    }

    const bool delete_call = IsSkiaBlock(args.queue);
    const bool has_marker  = SubmitHasFrameEndMarker(args.submitCount, &args.pSubmits);

    if (delete_call)
    {
        SetDeleteCurrentCall();
    }
    else if (has_marker)
    {
        frame_end_marker_blocks_to_insert_.insert(block_index_);
    }
}

void VulkanSkiaModifier::Process_vkQueueSubmit2(const ApiCallInfo& call_info, args::QueueSubmit2& args)
{
    if (IsModificationPass())
    {
        AppendFrameEndMarkerForCurrentBlock();
        return;
    }

    const bool delete_call = IsSkiaBlock(args.queue);
    const bool has_marker  = Submit2HasFrameEndMarker(args.submitCount, &args.pSubmits);

    if (delete_call)
    {
        SetDeleteCurrentCall();
    }
    else if (has_marker)
    {
        frame_end_marker_blocks_to_insert_.insert(block_index_);
    }
}

void VulkanSkiaModifier::Process_vkQueueSubmit2KHR(const ApiCallInfo& call_info, args::QueueSubmit2KHR& args)
{
    if (IsModificationPass())
    {
        AppendFrameEndMarkerForCurrentBlock();
        return;
    }

    const bool delete_call = IsSkiaBlock(args.queue);
    const bool has_marker  = Submit2HasFrameEndMarker(args.submitCount, &args.pSubmits);

    if (delete_call)
    {
        SetDeleteCurrentCall();
    }
    else if (has_marker)
    {
        frame_end_marker_blocks_to_insert_.insert(block_index_);
    }
}

void VulkanSkiaModifier::Process_vkFrameBoundaryANDROID(const ApiCallInfo& call_info, args::FrameBoundaryANDROID& args)
{
    if (IsModificationPass())
    {
        AppendFrameEndMarkerForCurrentBlock();
        return;
    }

    const bool delete_call = IsSkiaBlock(args.device);

    if (delete_call)
    {
        SetDeleteCurrentCall();
    }
    else
    {
        frame_end_marker_blocks_to_insert_.insert(block_index_);
    }
}

void VulkanSkiaModifier::Process_vkBeginCommandBuffer(const ApiCallInfo& call_info, args::BeginCommandBuffer& args)
{
    if (IsModificationPass())
    {
        return;
    }

    frame_boundary_command_buffers_.erase(args.commandBuffer);
    if (IsSkiaBlock(args.commandBuffer))
    {
        SetDeleteCurrentCall();
    }
}

void VulkanSkiaModifier::Process_vkResetCommandBuffer(const ApiCallInfo& call_info, args::ResetCommandBuffer& args)
{
    if (IsModificationPass())
    {
        return;
    }

    frame_boundary_command_buffers_.erase(args.commandBuffer);
    if (IsSkiaBlock(args.commandBuffer))
    {
        SetDeleteCurrentCall();
    }
}

void VulkanSkiaModifier::Process_vkCmdDebugMarkerInsertEXT(const ApiCallInfo&             call_info,
                                                           args::CmdDebugMarkerInsertEXT& args)
{
    if (IsModificationPass())
    {
        return;
    }

    const VkDebugMarkerMarkerInfoEXT* marker_info = args.pMarkerInfo.GetPointer();
    if ((marker_info != nullptr) && ContainsVrFrameDelimiter(marker_info->pMarkerName))
    {
        frame_boundary_command_buffers_.insert(args.commandBuffer);
    }

    if (IsSkiaBlock(args.commandBuffer))
    {
        SetDeleteCurrentCall();
    }
}

void VulkanSkiaModifier::Process_vkCmdInsertDebugUtilsLabelEXT(const ApiCallInfo&                 call_info,
                                                               args::CmdInsertDebugUtilsLabelEXT& args)
{
    if (IsModificationPass())
    {
        return;
    }

    const VkDebugUtilsLabelEXT* label_info = args.pLabelInfo.GetPointer();
    if ((label_info != nullptr) && ContainsVrFrameDelimiter(label_info->pLabelName))
    {
        frame_boundary_command_buffers_.insert(args.commandBuffer);
    }

    if (IsSkiaBlock(args.commandBuffer))
    {
        SetDeleteCurrentCall();
    }
}

bool VulkanSkiaModifier::CanOptimize()
{
    bool skivkOptimize = true;
    if (!not_skiavk_instance) // only include skiavk instance
        skivkOptimize = false;
    if (!skiavk_instance) // no skiavk instance
        skivkOptimize = false;
    GFXRECON_WRITE_CONSOLE("skiavk optimization is %s, remove block count: %u",
                           skivkOptimize ? "true" : "false",
                           skiavkindex2remove.size());

    return skivkOptimize;
}

bool VulkanSkiaModifier::IsSkiaBlock(format::HandleId handle)
{
    auto it0 = skia_device2queue.find(handle);
    if (it0 != skia_device2queue.end())
    {
        return true;
    }

    for (auto& e : skia_command_pool2command_buffer)
    {
        auto it1 = std::find(e.second.begin(), e.second.end(), handle);
        if (it1 != e.second.end())
        {
            return true;
        }
    }

    auto it3 = skiavk_instance2physical_device.find(handle);
    if (it3 != skiavk_instance2physical_device.end())
    {
        return true;
    }

    for (auto& e : skiavk_instance2physical_device)
    {
        auto it = std::find(e.second.begin(), e.second.end(), handle);
        if (it != e.second.end())
        {
            return true;
        }
    }

    for (auto& e : skiavk_physical_device2device)
    {
        auto it = std::find(e.second.begin(), e.second.end(), handle);
        if (it != e.second.end())
        {
            return true;
        }
    }

    for (auto& e : skia_device2queue)
    {
        auto it5 = std::find(e.second.begin(), e.second.end(), handle);
        if (it5 != e.second.end())
        {
            return true;
        }
    }
    return false;
}

void VulkanSkiaModifier::Process_vkCreateInstance(const ApiCallInfo& call_info, args::CreateInstance& args)
{
    if (IsModificationPass())
        return;

    const VkInstanceCreateInfo* pVkInstanceCreateInfo = args.pCreateInfo.GetPointer();

    if (pVkInstanceCreateInfo == nullptr)
    {
        return;
    }

    bool name_match = false;

    if (pVkInstanceCreateInfo->pApplicationInfo != nullptr)
    {
        for (const auto& app_name : VulkanSkiaModifier::app_name_array)
        {
            if (pVkInstanceCreateInfo->pApplicationInfo->pApplicationName != nullptr &&
                strcmp(pVkInstanceCreateInfo->pApplicationInfo->pApplicationName, app_name.c_str()) == 0)
            {
                name_match = true;
            }
            else if (pVkInstanceCreateInfo->pApplicationInfo->pEngineName != nullptr &&
                     strcmp(pVkInstanceCreateInfo->pApplicationInfo->pEngineName, app_name.c_str()) == 0)
            {
                name_match = true;
            }

            if (name_match)
            {
                break;
            }
        }
    }

    bool remove = name_match;
    if (keep_device_instance_mode_)
    {
        remove = !name_match;
    }

    if (remove)
    {
        std::vector<format::HandleId> physical_device;
        skiavk_instance2physical_device[*(args.pInstance.GetPointer())] = physical_device;
        SetDeleteCurrentCall();
        skiavk_instance = true;
        return;
    }

    not_skiavk_instance = true;
}

void VulkanSkiaModifier::Process_vkDestroyInstance(const ApiCallInfo& call_info, args::DestroyInstance& args)
{
    if (IsModificationPass())
        return;
    auto                          it0 = skiavk_instance2physical_device.find(args.instance);
    std::vector<format::HandleId> physical_devices;
    if (it0 != skiavk_instance2physical_device.end())
    {
        SetDeleteCurrentCall();
        physical_devices = it0->second;
        skiavk_instance2physical_device.erase(it0);
    }
    for (auto& e : physical_devices)
    {
        std::vector<format::HandleId> devices;
        auto                          it1 = skiavk_physical_device2device.find(e);
        if (it1 != skiavk_physical_device2device.end())
        {
            devices = it1->second;
            skiavk_physical_device2device.erase(it1);
        }

        for (auto& e : devices)
        {
            auto it2 = skia_device2queue.find(e);
            if (it2 != skia_device2queue.end())
            {
                skia_device2queue.erase(it2);
            }
            std::vector<format::HandleId> command_pool;
            auto                          it3 = skia_device2command_pool.find(e);
            if (it3 != skia_device2command_pool.end())
            {
                command_pool = it3->second;
                skia_device2command_pool.erase(it3);
            }
            for (auto& cp : command_pool)
            {
                auto it4 = skia_command_pool2command_buffer.find(cp);
                if (it4 != skia_command_pool2command_buffer.end())
                {
                    skia_command_pool2command_buffer.erase(it4);
                }
            }
            auto it5 = skia_device2memory.find(e);
            if (it5 != skia_device2memory.end())
            {
                skia_device2memory.erase(it5);
            }
            auto it6 = skia_device2buffer.find(e);
            if (it6 != skia_device2buffer.end())
            {
                skia_device2buffer.erase(it6);
            }
        }
    }
}

void VulkanSkiaModifier::Process_vkEnumeratePhysicalDevices(const ApiCallInfo&              call_info,
                                                            args::EnumeratePhysicalDevices& args)
{
    if (IsModificationPass())
        return;
    auto it = skiavk_instance2physical_device.find(args.instance);
    if (it != skiavk_instance2physical_device.end())
    {
        SetDeleteCurrentCall();
        if (!args.pPhysicalDevices.IsNull())
        {
            for (int i = 0; i < *args.pPhysicalDeviceCount.GetPointer(); i++)
            {
                it->second.push_back(((args.pPhysicalDevices.GetPointer()))[i]);
            }
        }
    }
}

void VulkanSkiaModifier::Process_vkCreateDevice(const ApiCallInfo& call_info, args::CreateDevice& args)
{
    if (IsModificationPass())
        return;
    for (auto& e : skiavk_instance2physical_device)
    {
        auto it = std::find(e.second.begin(), e.second.end(), args.physicalDevice);
        if (it != e.second.end())
        {
            SetDeleteCurrentCall();
            skiavk_physical_device2device[args.physicalDevice].push_back(*args.pDevice.GetPointer());
            break;
        }
    }
}

void VulkanSkiaModifier::Process_vkDestroyDevice(const ApiCallInfo& call_info, args::DestroyDevice& args)
{
    if (IsModificationPass())
        return;
    for (auto& e : skiavk_physical_device2device)
    {
        auto it = std::find(e.second.begin(), e.second.end(), args.device);
        if (it != e.second.end())
        {
            SetDeleteCurrentCall();
            e.second.erase(it);
            break;
        }
    }
    auto it2 = skia_device2queue.find(args.device);
    if (it2 != skia_device2queue.end())
    {
        skia_device2queue.erase(it2);
    }
    std::vector<format::HandleId> command_pool;
    auto                          it3 = skia_device2command_pool.find(args.device);
    if (it3 != skia_device2command_pool.end())
    {
        command_pool = it3->second;
        skia_device2command_pool.erase(it3);
    }
    for (auto& cp : command_pool)
    {
        auto it4 = skia_command_pool2command_buffer.find(cp);
        if (it4 != skia_command_pool2command_buffer.end())
        {
            skia_command_pool2command_buffer.erase(it4);
        }
    }
    auto it5 = skia_device2memory.find(args.device);
    if (it5 != skia_device2memory.end())
    {
        skia_device2memory.erase(it5);
    }
    auto it6 = skia_device2buffer.find(args.device);
    if (it6 != skia_device2buffer.end())
    {
        skia_device2buffer.erase(it6);
    }
}

void VulkanSkiaModifier::Process_vkGetDeviceQueue(const ApiCallInfo& call_info, args::GetDeviceQueue& args)
{
    if (IsModificationPass())
        return;
    for (auto& e : skiavk_physical_device2device)
    {
        auto it = std::find(e.second.begin(), e.second.end(), args.device);
        if (it != e.second.end())
        {
            SetDeleteCurrentCall();
            skia_device2queue[args.device].push_back((*args.pQueue.GetPointer()));
            break;
        }
    }
}

void VulkanSkiaModifier::Process_vkGetDeviceQueue2(const ApiCallInfo& call_info, args::GetDeviceQueue2& args)
{
    if (IsModificationPass())
        return;
    for (auto& e : skiavk_physical_device2device)
    {
        auto it = std::find(e.second.begin(), e.second.end(), args.device);
        if (it != e.second.end())
        {
            SetDeleteCurrentCall();
            skia_device2queue[args.device].push_back((*args.pQueue.GetPointer()));
            break;
        }
    }
}

void VulkanSkiaModifier::Process_vkCreateCommandPool(const ApiCallInfo& call_info, args::CreateCommandPool& args)
{
    if (IsModificationPass())
        return;
    for (auto& e : skiavk_physical_device2device)
    {
        auto it = std::find(e.second.begin(), e.second.end(), args.device);
        if (it != e.second.end())
        {
            SetDeleteCurrentCall();
            skia_device2command_pool[args.device].push_back((*args.pCommandPool.GetPointer()));
            break;
        }
    }
}

void VulkanSkiaModifier::Process_vkDestroyCommandPool(const ApiCallInfo& call_info, args::DestroyCommandPool& args)
{
    if (IsModificationPass())
        return;
    auto it = skia_device2command_pool.find(args.device);
    if (it != skia_device2command_pool.end())
    {
        SetDeleteCurrentCall();
        auto it1 = std::find(it->second.begin(), it->second.end(), args.commandPool);
        if (it1 != it->second.end())
        {
            it->second.erase(it1);
            auto it2 = skia_command_pool2command_buffer.find(args.commandPool);
            if (it2 != skia_command_pool2command_buffer.end())
            {
                skia_command_pool2command_buffer.erase(it2);
            }
        }
    }
}

void VulkanSkiaModifier::Process_vkAllocateCommandBuffers(const ApiCallInfo&            call_info,
                                                          args::AllocateCommandBuffers& args)
{
    if (IsModificationPass())
        return;
    auto it = skia_device2queue.find(args.device);
    if (it != skia_device2queue.end())
    {
        SetDeleteCurrentCall();
        VkCommandBufferAllocateInfo* pVkCommandBufferAllocateInfo =
            (VkCommandBufferAllocateInfo*)args.pAllocateInfo.GetPointer();
        for (int i = 0; i < pVkCommandBufferAllocateInfo->commandBufferCount; i++)
        {
            skia_command_pool2command_buffer[(args.pAllocateInfo.GetMetaStructPointer()->commandPool)].push_back(
                (args.pCommandBuffers.GetPointer())[i]);
        }
    }
}

void VulkanSkiaModifier::Process_vkFreeCommandBuffers(const ApiCallInfo& call_info, args::FreeCommandBuffers& args)
{
    if (IsModificationPass())
        return;
    auto it = skia_command_pool2command_buffer.find(args.commandPool);
    if (it != skia_command_pool2command_buffer.end())
    {
        SetDeleteCurrentCall();
        for (int i = 0; i < args.commandBufferCount; i++)
        {
            auto it1 = std::find(it->second.begin(), it->second.end(), ((args.pCommandBuffers.GetPointer()))[i]);
            if (it1 != it->second.end())
            {
                it->second.erase(it1);
            }
        }
    }
}

void VulkanSkiaModifier::Process_vkAllocateMemory(const ApiCallInfo& call_info, args::AllocateMemory& args)
{
    if (IsModificationPass())
        return;

    if (IsSkiaBlock(args.device))
    {
        SetDeleteCurrentCall();
        skia_device2memory[args.device].push_back(*args.pMemory.GetPointer());
    }
}

void VulkanSkiaModifier::Process_vkFreeMemory(const ApiCallInfo& call_info, args::FreeMemory& args)
{
    if (IsModificationPass())
        return;

    if (IsSkiaBlock(args.device))
    {
        SetDeleteCurrentCall();
    }

    auto it = skia_device2memory.find(args.device);
    if (it != skia_device2memory.end())
    {
        auto it1 = std::find(it->second.begin(), it->second.end(), args.memory);
        if (it1 != it->second.end())
        {
            it->second.erase(it1);
        }
    }
}

void VulkanSkiaModifier::Process_vkCreateAndroidSurfaceKHR(const ApiCallInfo&             call_info,
                                                           args::CreateAndroidSurfaceKHR& args)
{
    if (IsModificationPass())
        return;
    auto it = skiavk_instance2physical_device.find(args.instance);
    if (it != skiavk_instance2physical_device.end())
    {
        SetDeleteCurrentCall();
        skia_instance2surface[args.instance].push_back(*args.pSurface.GetPointer());
    }
}

void VulkanSkiaModifier::Process_vkCreateBuffer(const ApiCallInfo& call_info, args::CreateBuffer& args)
{
    if (IsModificationPass())
        return;
    auto it = skia_device2queue.find(args.device);
    if (it != skia_device2queue.end())
    {
        SetDeleteCurrentCall();
        skia_device2buffer[args.device].push_back(*args.pBuffer.GetPointer());
    }
}

void VulkanSkiaModifier::Process_vkDestroyBuffer(const ApiCallInfo& call_info, args::DestroyBuffer& args)
{
    if (IsModificationPass())
        return;
    auto it = skia_device2buffer.find(args.device);
    if (it != skia_device2buffer.end())
    {
        SetDeleteCurrentCall();
        auto it1 = std::find(it->second.begin(), it->second.end(), args.buffer);
        if (it1 != it->second.end())
        {
            it->second.erase(it1);
        }
    }
}

void VulkanSkiaModifier::ProcessSetDevicePropertiesCommand(format::HandleId   physical_device_id,
                                                           uint32_t           api_version,
                                                           uint32_t           driver_version,
                                                           uint32_t           vendor_id,
                                                           uint32_t           device_id,
                                                           uint32_t           device_type,
                                                           const uint8_t      pipeline_cache_uuid[format::kUuidSize],
                                                           const std::string& device_name)
{
    if (IsModificationPass())
        return;
    for (auto& e : skiavk_instance2physical_device)
    {
        auto it = std::find(e.second.begin(), e.second.end(), physical_device_id);
        if (it != e.second.end())
        {
            skiavkindex2remove[block_index_] = true;
            break;
        }
    }
}

void VulkanSkiaModifier::ProcessFillMemoryCommand(uint64_t       memory_id,
                                                  uint64_t       offset,
                                                  uint64_t       size,
                                                  const uint8_t* data)
{
    if (IsModificationPass())
        return;
    for (auto& e : skia_device2memory)
    {
        auto it = std::find(e.second.begin(), e.second.end(), memory_id);
        if (it != e.second.end())
        {
            skiavkindex2remove[block_index_] = true;
            break;
        }
    }
}

void VulkanSkiaModifier::ProcessCreateHardwareBufferCommand(
    format::HandleId                                    device_id,
    format::HandleId                                    memory_id,
    uint64_t                                            buffer_id,
    uint32_t                                            format,
    uint32_t                                            width,
    uint32_t                                            height,
    uint32_t                                            stride,
    uint64_t                                            usage,
    uint32_t                                            layers,
    const std::vector<format::HardwareBufferPlaneInfo>& plane_info)
{
    if (IsModificationPass())
        return;
    auto it2 = skia_device2queue.find(device_id);
    if (it2 != skia_device2queue.end())
    {
        skia_device2buffer[device_id].push_back(buffer_id);
        skiavkindex2remove[block_index_] = true;
        return;
    }
    for (auto& e : skia_device2memory)
    {
        auto it = std::find(e.second.begin(), e.second.end(), memory_id);
        if (it != e.second.end())
        {
            skia_device2buffer[device_id].push_back(buffer_id);
            skiavkindex2remove[block_index_] = true;
            return;
        }
    }
}

void VulkanSkiaModifier::ProcessFixDeviceAddressCommand(const format::FixDeviceAddressCommandHeader&    header,
                                                        const std::vector<format::AddressLocationInfo>& infos)
{
    if (IsModificationPass())
        return;
    auto it = skia_device2queue.find(header.relation_id);
    if (it != skia_device2queue.end())
    {
        skiavkindex2remove[block_index_] = true;
    }
    else
    {
        for (auto& e : skia_device2memory)
        {
            auto it1 = std::find(e.second.begin(), e.second.end(), header.relation_id);
            if (it1 != e.second.end())
            {
                skiavkindex2remove[block_index_] = true;
                break;
            }
        }
    }
}

void VulkanSkiaModifier::ProcessFixShaderGroupHandleCommand(const format::FixShaderGroupHandleCommandHeader&     header,
                                                            const std::vector<format::ShaderHandleLocationInfo>& infos)
{
    if (IsModificationPass())
        return;
    auto it = skia_device2queue.find(header.relation_id);
    if (it != skia_device2queue.end())
    {
        skiavkindex2remove[block_index_] = true;
    }
    else
    {
        for (auto& e : skia_device2memory)
        {
            auto it1 = std::find(e.second.begin(), e.second.end(), header.relation_id);
            if (it1 != e.second.end())
            {
                skiavkindex2remove[block_index_] = true;
                break;
            }
        }
    }
}

void VulkanSkiaModifier::ProcessResizeWindowCommand(format::HandleId surface_id, uint32_t width, uint32_t height)
{
    if (IsModificationPass())
        return;
    for (auto& e : skia_instance2surface)
    {
        auto it = std::find(e.second.begin(), e.second.end(), surface_id);
        if (it != e.second.end())
        {
            skiavkindex2remove[block_index_] = true;
            break;
        }
    }
}

void VulkanSkiaModifier::ProcessResizeWindowCommand2(format::HandleId surface_id,
                                                     uint32_t         width,
                                                     uint32_t         height,
                                                     uint32_t         pre_transform)
{
    if (IsModificationPass())
        return;
    for (auto& e : skia_instance2surface)
    {
        auto it = std::find(e.second.begin(), e.second.end(), surface_id);
        if (it != e.second.end())
        {
            skiavkindex2remove[block_index_] = true;
            break;
        }
    }
}

void VulkanSkiaModifier::ProcessDestroyHardwareBufferCommand(uint64_t buffer_id)
{
    if (IsModificationPass())
        return;
    for (auto& e : skia_device2buffer)
    {
        auto it = std::find(e.second.begin(), e.second.end(), buffer_id);
        if (it != e.second.end())
        {
            e.second.erase(it);
            skiavkindex2remove[block_index_] = true;
            break;
        }
    }
}

void VulkanSkiaModifier::ProcessSetDeviceMemoryPropertiesCommand(
    format::HandleId                             physical_device_id,
    const std::vector<format::DeviceMemoryType>& memory_types,
    const std::vector<format::DeviceMemoryHeap>& memory_heaps)
{
    if (IsModificationPass())
        return;
    for (auto& e : skiavk_instance2physical_device)
    {
        auto it = std::find(e.second.begin(), e.second.end(), physical_device_id);
        if (it != e.second.end())
        {
            skiavkindex2remove[block_index_] = true;
            break;
        }
    }
}

void VulkanSkiaModifier::ProcessSetOpaqueAddressCommand(format::HandleId device_id,
                                                        format::HandleId object_id,
                                                        uint64_t         address)
{
    if (IsModificationPass())
        return;
    auto it = skia_device2queue.find(device_id);
    if (it != skia_device2queue.end())
    {
        skiavkindex2remove[block_index_] = true;
    }
}

void VulkanSkiaModifier::ProcessSetRayTracingShaderGroupHandlesCommand(format::HandleId device_id,
                                                                       format::HandleId pipeline_id,
                                                                       size_t           data_size,
                                                                       const uint8_t*   data)
{
    if (IsModificationPass())
        return;
    auto it = skia_device2queue.find(device_id);
    if (it != skia_device2queue.end())
    {
        skiavkindex2remove[block_index_] = true;
    }
}

void VulkanSkiaModifier::ProcessSetSwapchainImageStateCommand(
    format::HandleId                                    device_id,
    format::HandleId                                    swapchain_id,
    uint32_t                                            last_presented_image,
    const std::vector<format::SwapchainImageStateInfo>& image_state)
{
    if (IsModificationPass())
        return;
    auto it = skia_device2queue.find(device_id);
    if (it != skia_device2queue.end())
    {
        skiavkindex2remove[block_index_] = true;
    }
}

void VulkanSkiaModifier::ProcessBeginResourceInitCommand(format::HandleId device_id,
                                                         uint64_t         max_resource_size,
                                                         uint64_t         max_copy_size)
{
    if (IsModificationPass())
        return;
    auto it = skia_device2queue.find(device_id);
    if (it != skia_device2queue.end())
    {
        skiavkindex2remove[block_index_] = true;
    }
}

void VulkanSkiaModifier::ProcessEndResourceInitCommand(format::HandleId device_id)
{
    if (IsModificationPass())
        return;
    auto it = skia_device2queue.find(device_id);
    if (it != skia_device2queue.end())
    {
        skiavkindex2remove[block_index_] = true;
    }
}

void VulkanSkiaModifier::ProcessInitBufferCommand(format::HandleId device_id,
                                                  format::HandleId buffer_id,
                                                  uint64_t         data_size,
                                                  const uint8_t*   data)
{
    if (IsModificationPass())
        return;
    auto it = skia_device2queue.find(device_id);
    if (it != skia_device2queue.end())
    {
        skiavkindex2remove[block_index_] = true;
    }
}

void VulkanSkiaModifier::ProcessInitImageCommand(format::HandleId             device_id,
                                                 format::HandleId             image_id,
                                                 uint64_t                     data_size,
                                                 uint32_t                     aspect,
                                                 uint32_t                     layout,
                                                 const std::vector<uint64_t>& level_sizes,
                                                 const uint8_t*               data)
{
    if (IsModificationPass())
        return;
    auto it = skia_device2queue.find(device_id);
    if (it != skia_device2queue.end())
    {
        skiavkindex2remove[block_index_] = true;
    }
}

void VulkanSkiaModifier::ProcessInitSubresourceCommand(const format::InitSubresourceCommandHeader& command_header,
                                                       const uint8_t*                              data)
{
    if (IsModificationPass())
        return;
    auto it = skia_device2queue.find(command_header.device_id);
    if (it != skia_device2queue.end())
    {
        skiavkindex2remove[block_index_] = true;
    }
}

void VulkanSkiaModifier::ProcessVulkanBuildAccelerationStructuresCommand(
    format::HandleId                                                           device_id,
    uint32_t                                                                   info_count,
    StructPointerDecoder<Decoded_VkAccelerationStructureBuildGeometryInfoKHR>* geometry_infos,
    StructPointerDecoder<Decoded_VkAccelerationStructureBuildRangeInfoKHR*>*   range_infos,
    std::vector<std::vector<VkAccelerationStructureInstanceKHR>>&              instance_buffers_data)
{
    if (IsModificationPass())
        return;
    auto it = skia_device2queue.find(device_id);
    if (it != skia_device2queue.end())
    {
        skiavkindex2remove[block_index_] = true;
    }
}

void VulkanSkiaModifier::ProcessVulkanCopyAccelerationStructuresCommand(
    format::HandleId device_id, StructPointerDecoder<Decoded_VkCopyAccelerationStructureInfoKHR>* copy_infos)
{
    if (IsModificationPass())
        return;
    auto it = skia_device2queue.find(device_id);
    if (it != skia_device2queue.end())
    {
        skiavkindex2remove[block_index_] = true;
    }
}

void VulkanSkiaModifier::ProcessVulkanWriteAccelerationStructuresPropertiesCommand(
    format::HandleId device_id, VkQueryType query_type, format::HandleId acceleration_structure_id)
{
    if (IsModificationPass())
        return;
    auto it = skia_device2queue.find(device_id);
    if (it != skia_device2queue.end())
    {
        skiavkindex2remove[block_index_] = true;
    }
}

GFXRECON_END_NAMESPACE(decode)
GFXRECON_END_NAMESPACE(gfxrecon)
