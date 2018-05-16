/*
** Copyright (c) 2018 LunarG, Inc.
**
** Licensed under the Apache License, Version 2.0 (the "License");
** you may not use this file except in compliance with the License.
** You may obtain a copy of the License at
**
**     http://www.apache.org/licenses/LICENSE-2.0
**
** Unless required by applicable law or agreed to in writing, software
** distributed under the License is distributed on an "AS IS" BASIS,
** WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
** See the License for the specific language governing permissions and
** limitations under the License.
*/

/*
** The IDs defined by this header should not be modified.  New API call
** IDs should always be appended to the existing API call ID list.
**
*/

#ifndef BRIMSTONE_FORMAT_API_CALL_ID_H
#define BRIMSTONE_FORMAT_API_CALL_ID_H

#include "util/defines.h"

BRIMSTONE_BEGIN_NAMESPACE(brimstone)
BRIMSTONE_BEGIN_NAMESPACE(format)

enum ApiCallId : uint32_t
{
    ApiCallId_Unknown                                                                               = 0x0000,

    // Vulkan API (range 0x1000..0x1fff)
    ApiCallId_vkCreateInstance                                                                      = 0x1000,
    ApiCallId_vkDestroyInstance                                                                     = 0x1001,
    ApiCallId_vkEnumeratePhysicalDevices                                                            = 0x1002,
    ApiCallId_vkGetPhysicalDeviceFeatures                                                           = 0x1003,
    ApiCallId_vkGetPhysicalDeviceFormatProperties                                                   = 0x1004,
    ApiCallId_vkGetPhysicalDeviceImageFormatProperties                                              = 0x1005,
    ApiCallId_vkGetPhysicalDeviceProperties                                                         = 0x1006,
    ApiCallId_vkGetPhysicalDeviceQueueFamilyProperties                                              = 0x1007,
    ApiCallId_vkGetPhysicalDeviceMemoryProperties                                                   = 0x1008,
    ApiCallId_vkGetInstanceProcAddr                                                                 = 0x1009,
    ApiCallId_vkGetDeviceProcAddr                                                                   = 0x100a,
    ApiCallId_vkCreateDevice                                                                        = 0x100b,
    ApiCallId_vkDestroyDevice                                                                       = 0x100c,
    ApiCallId_vkEnumerateInstanceExtensionProperties                                                = 0x100d,
    ApiCallId_vkEnumerateDeviceExtensionProperties                                                  = 0x100e,
    ApiCallId_vkEnumerateInstanceLayerProperties                                                    = 0x100f,
    ApiCallId_vkEnumerateDeviceLayerProperties                                                      = 0x1010,
    ApiCallId_vkGetDeviceQueue                                                                      = 0x1011,
    ApiCallId_vkQueueSubmit                                                                         = 0x1012,
    ApiCallId_vkQueueWaitIdle                                                                       = 0x1013,
    ApiCallId_vkDeviceWaitIdle                                                                      = 0x1014,
    ApiCallId_vkAllocateMemory                                                                      = 0x1015,
    ApiCallId_vkFreeMemory                                                                          = 0x1016,
    ApiCallId_vkMapMemory                                                                           = 0x1017,
    ApiCallId_vkUnmapMemory                                                                         = 0x1018,
    ApiCallId_vkFlushMappedMemoryRanges                                                             = 0x1019,
    ApiCallId_vkInvalidateMappedMemoryRanges                                                        = 0x101a,
    ApiCallId_vkGetDeviceMemoryCommitment                                                           = 0x101b,
    ApiCallId_vkBindBufferMemory                                                                    = 0x101c,
    ApiCallId_vkBindImageMemory                                                                     = 0x101d,
    ApiCallId_vkGetBufferMemoryRequirements                                                         = 0x101e,
    ApiCallId_vkGetImageMemoryRequirements                                                          = 0x101f,
    ApiCallId_vkGetImageSparseMemoryRequirements                                                    = 0x1020,
    ApiCallId_vkGetPhysicalDeviceSparseImageFormatProperties                                        = 0x1021,
    ApiCallId_vkQueueBindSparse                                                                     = 0x1022,
    ApiCallId_vkCreateFence                                                                         = 0x1023,
    ApiCallId_vkDestroyFence                                                                        = 0x1024,
    ApiCallId_vkResetFences                                                                         = 0x1025,
    ApiCallId_vkGetFenceStatus                                                                      = 0x1026,
    ApiCallId_vkWaitForFences                                                                       = 0x1027,
    ApiCallId_vkCreateSemaphore                                                                     = 0x1028,
    ApiCallId_vkDestroySemaphore                                                                    = 0x1029,
    ApiCallId_vkCreateEvent                                                                         = 0x102a,
    ApiCallId_vkDestroyEvent                                                                        = 0x102b,
    ApiCallId_vkGetEventStatus                                                                      = 0x102c,
    ApiCallId_vkSetEvent                                                                            = 0x102d,
    ApiCallId_vkResetEvent                                                                          = 0x102e,
    ApiCallId_vkCreateQueryPool                                                                     = 0x102f,
    ApiCallId_vkDestroyQueryPool                                                                    = 0x1030,
    ApiCallId_vkGetQueryPoolResults                                                                 = 0x1031,
    ApiCallId_vkCreateBuffer                                                                        = 0x1032,
    ApiCallId_vkDestroyBuffer                                                                       = 0x1033,
    ApiCallId_vkCreateBufferView                                                                    = 0x1034,
    ApiCallId_vkDestroyBufferView                                                                   = 0x1035,
    ApiCallId_vkCreateImage                                                                         = 0x1036,
    ApiCallId_vkDestroyImage                                                                        = 0x1037,
    ApiCallId_vkGetImageSubresourceLayout                                                           = 0x1038,
    ApiCallId_vkCreateImageView                                                                     = 0x1039,
    ApiCallId_vkDestroyImageView                                                                    = 0x103a,
    ApiCallId_vkCreateShaderModule                                                                  = 0x103b,
    ApiCallId_vkDestroyShaderModule                                                                 = 0x103c,
    ApiCallId_vkCreatePipelineCache                                                                 = 0x103d,
    ApiCallId_vkDestroyPipelineCache                                                                = 0x103e,
    ApiCallId_vkGetPipelineCacheData                                                                = 0x103f,
    ApiCallId_vkMergePipelineCaches                                                                 = 0x1040,
    ApiCallId_vkCreateGraphicsPipelines                                                             = 0x1041,
    ApiCallId_vkCreateComputePipelines                                                              = 0x1042,
    ApiCallId_vkDestroyPipeline                                                                     = 0x1043,
    ApiCallId_vkCreatePipelineLayout                                                                = 0x1044,
    ApiCallId_vkDestroyPipelineLayout                                                               = 0x1045,
    ApiCallId_vkCreateSampler                                                                       = 0x1046,
    ApiCallId_vkDestroySampler                                                                      = 0x1047,
    ApiCallId_vkCreateDescriptorSetLayout                                                           = 0x1048,
    ApiCallId_vkDestroyDescriptorSetLayout                                                          = 0x1049,
    ApiCallId_vkCreateDescriptorPool                                                                = 0x104a,
    ApiCallId_vkDestroyDescriptorPool                                                               = 0x104b,
    ApiCallId_vkResetDescriptorPool                                                                 = 0x104c,
    ApiCallId_vkAllocateDescriptorSets                                                              = 0x104d,
    ApiCallId_vkFreeDescriptorSets                                                                  = 0x104e,
    ApiCallId_vkUpdateDescriptorSets                                                                = 0x104f,
    ApiCallId_vkCreateFramebuffer                                                                   = 0x1050,
    ApiCallId_vkDestroyFramebuffer                                                                  = 0x1051,
    ApiCallId_vkCreateRenderPass                                                                    = 0x1052,
    ApiCallId_vkDestroyRenderPass                                                                   = 0x1053,
    ApiCallId_vkGetRenderAreaGranularity                                                            = 0x1054,
    ApiCallId_vkCreateCommandPool                                                                   = 0x1055,
    ApiCallId_vkDestroyCommandPool                                                                  = 0x1056,
    ApiCallId_vkResetCommandPool                                                                    = 0x1057,
    ApiCallId_vkAllocateCommandBuffers                                                              = 0x1058,
    ApiCallId_vkFreeCommandBuffers                                                                  = 0x1059,
    ApiCallId_vkBeginCommandBuffer                                                                  = 0x105a,
    ApiCallId_vkEndCommandBuffer                                                                    = 0x105b,
    ApiCallId_vkResetCommandBuffer                                                                  = 0x105c,
    ApiCallId_vkCmdBindPipeline                                                                     = 0x105d,
    ApiCallId_vkCmdSetViewport                                                                      = 0x105e,
    ApiCallId_vkCmdSetScissor                                                                       = 0x105f,
    ApiCallId_vkCmdSetLineWidth                                                                     = 0x1060,
    ApiCallId_vkCmdSetDepthBias                                                                     = 0x1061,
    ApiCallId_vkCmdSetBlendConstants                                                                = 0x1062,
    ApiCallId_vkCmdSetDepthBounds                                                                   = 0x1063,
    ApiCallId_vkCmdSetStencilCompareMask                                                            = 0x1064,
    ApiCallId_vkCmdSetStencilWriteMask                                                              = 0x1065,
    ApiCallId_vkCmdSetStencilReference                                                              = 0x1066,
    ApiCallId_vkCmdBindDescriptorSets                                                               = 0x1067,
    ApiCallId_vkCmdBindIndexBuffer                                                                  = 0x1068,
    ApiCallId_vkCmdBindVertexBuffers                                                                = 0x1069,
    ApiCallId_vkCmdDraw                                                                             = 0x106a,
    ApiCallId_vkCmdDrawIndexed                                                                      = 0x106b,
    ApiCallId_vkCmdDrawIndirect                                                                     = 0x106c,
    ApiCallId_vkCmdDrawIndexedIndirect                                                              = 0x106d,
    ApiCallId_vkCmdDispatch                                                                         = 0x106e,
    ApiCallId_vkCmdDispatchIndirect                                                                 = 0x106f,
    ApiCallId_vkCmdCopyBuffer                                                                       = 0x1070,
    ApiCallId_vkCmdCopyImage                                                                        = 0x1071,
    ApiCallId_vkCmdBlitImage                                                                        = 0x1072,
    ApiCallId_vkCmdCopyBufferToImage                                                                = 0x1073,
    ApiCallId_vkCmdCopyImageToBuffer                                                                = 0x1074,
    ApiCallId_vkCmdUpdateBuffer                                                                     = 0x1075,
    ApiCallId_vkCmdFillBuffer                                                                       = 0x1076,
    ApiCallId_vkCmdClearColorImage                                                                  = 0x1077,
    ApiCallId_vkCmdClearDepthStencilImage                                                           = 0x1078,
    ApiCallId_vkCmdClearAttachments                                                                 = 0x1079,
    ApiCallId_vkCmdResolveImage                                                                     = 0x107a,
    ApiCallId_vkCmdSetEvent                                                                         = 0x107b,
    ApiCallId_vkCmdResetEvent                                                                       = 0x107c,
    ApiCallId_vkCmdWaitEvents                                                                       = 0x107d,
    ApiCallId_vkCmdPipelineBarrier                                                                  = 0x107e,
    ApiCallId_vkCmdBeginQuery                                                                       = 0x107f,
    ApiCallId_vkCmdEndQuery                                                                         = 0x1080,
    ApiCallId_vkCmdResetQueryPool                                                                   = 0x1081,
    ApiCallId_vkCmdWriteTimestamp                                                                   = 0x1082,
    ApiCallId_vkCmdCopyQueryPoolResults                                                             = 0x1083,
    ApiCallId_vkCmdPushConstants                                                                    = 0x1084,
    ApiCallId_vkCmdBeginRenderPass                                                                  = 0x1085,
    ApiCallId_vkCmdNextSubpass                                                                      = 0x1086,
    ApiCallId_vkCmdEndRenderPass                                                                    = 0x1087,
    ApiCallId_vkCmdExecuteCommands                                                                  = 0x1088,
    ApiCallId_vkEnumerateInstanceVersion                                                            = 0x1089,
    ApiCallId_vkBindBufferMemory2                                                                   = 0x108a,
    ApiCallId_vkBindImageMemory2                                                                    = 0x108b,
    ApiCallId_vkGetDeviceGroupPeerMemoryFeatures                                                    = 0x108c,
    ApiCallId_vkCmdSetDeviceMask                                                                    = 0x108d,
    ApiCallId_vkCmdDispatchBase                                                                     = 0x108e,
    ApiCallId_vkEnumeratePhysicalDeviceGroups                                                       = 0x108f,
    ApiCallId_vkGetImageMemoryRequirements2                                                         = 0x1090,
    ApiCallId_vkGetBufferMemoryRequirements2                                                        = 0x1091,
    ApiCallId_vkGetImageSparseMemoryRequirements2                                                   = 0x1092,
    ApiCallId_vkGetPhysicalDeviceFeatures2                                                          = 0x1093,
    ApiCallId_vkGetPhysicalDeviceProperties2                                                        = 0x1094,
    ApiCallId_vkGetPhysicalDeviceFormatProperties2                                                  = 0x1095,
    ApiCallId_vkGetPhysicalDeviceImageFormatProperties2                                             = 0x1096,
    ApiCallId_vkGetPhysicalDeviceQueueFamilyProperties2                                             = 0x1097,
    ApiCallId_vkGetPhysicalDeviceMemoryProperties2                                                  = 0x1098,
    ApiCallId_vkGetPhysicalDeviceSparseImageFormatProperties2                                       = 0x1099,
    ApiCallId_vkTrimCommandPool                                                                     = 0x109a,
    ApiCallId_vkGetDeviceQueue2                                                                     = 0x109b,
    ApiCallId_vkCreateSamplerYcbcrConversion                                                        = 0x109c,
    ApiCallId_vkDestroySamplerYcbcrConversion                                                       = 0x109d,
    ApiCallId_vkCreateDescriptorUpdateTemplate                                                      = 0x109e,
    ApiCallId_vkDestroyDescriptorUpdateTemplate                                                     = 0x109f,
    ApiCallId_vkUpdateDescriptorSetWithTemplate                                                     = 0x10a0,
    ApiCallId_vkGetPhysicalDeviceExternalBufferProperties                                           = 0x10a1,
    ApiCallId_vkGetPhysicalDeviceExternalFenceProperties                                            = 0x10a2,
    ApiCallId_vkGetPhysicalDeviceExternalSemaphoreProperties                                        = 0x10a3,
    ApiCallId_vkGetDescriptorSetLayoutSupport                                                       = 0x10a4,
    ApiCallId_vkDestroySurfaceKHR                                                                   = 0x10a5,
    ApiCallId_vkGetPhysicalDeviceSurfaceSupportKHR                                                  = 0x10a6,
    ApiCallId_vkGetPhysicalDeviceSurfaceCapabilitiesKHR                                             = 0x10a7,
    ApiCallId_vkGetPhysicalDeviceSurfaceFormatsKHR                                                  = 0x10a8,
    ApiCallId_vkGetPhysicalDeviceSurfacePresentModesKHR                                             = 0x10a9,
    ApiCallId_vkCreateSwapchainKHR                                                                  = 0x10aa,
    ApiCallId_vkDestroySwapchainKHR                                                                 = 0x10ab,
    ApiCallId_vkGetSwapchainImagesKHR                                                               = 0x10ac,
    ApiCallId_vkAcquireNextImageKHR                                                                 = 0x10ad,
    ApiCallId_vkQueuePresentKHR                                                                     = 0x10ae,
    ApiCallId_vkGetDeviceGroupPresentCapabilitiesKHR                                                = 0x10af,
    ApiCallId_vkGetDeviceGroupSurfacePresentModesKHR                                                = 0x10b0,
    ApiCallId_vkGetPhysicalDevicePresentRectanglesKHR                                               = 0x10b1,
    ApiCallId_vkAcquireNextImage2KHR                                                                = 0x10b2,
    ApiCallId_vkGetPhysicalDeviceDisplayPropertiesKHR                                               = 0x10b3,
    ApiCallId_vkGetPhysicalDeviceDisplayPlanePropertiesKHR                                          = 0x10b4,
    ApiCallId_vkGetDisplayPlaneSupportedDisplaysKHR                                                 = 0x10b5,
    ApiCallId_vkGetDisplayModePropertiesKHR                                                         = 0x10b6,
    ApiCallId_vkCreateDisplayModeKHR                                                                = 0x10b7,
    ApiCallId_vkGetDisplayPlaneCapabilitiesKHR                                                      = 0x10b8,
    ApiCallId_vkCreateDisplayPlaneSurfaceKHR                                                        = 0x10b9,
    ApiCallId_vkCreateSharedSwapchainsKHR                                                           = 0x10ba,
    ApiCallId_vkCreateXlibSurfaceKHR                                                                = 0x10bb,
    ApiCallId_vkGetPhysicalDeviceXlibPresentationSupportKHR                                         = 0x10bc,
    ApiCallId_vkCreateXcbSurfaceKHR                                                                 = 0x10bd,
    ApiCallId_vkGetPhysicalDeviceXcbPresentationSupportKHR                                          = 0x10be,
    ApiCallId_vkCreateWaylandSurfaceKHR                                                             = 0x10bf,
    ApiCallId_vkGetPhysicalDeviceWaylandPresentationSupportKHR                                      = 0x10c0,
    ApiCallId_vkCreateMirSurfaceKHR                                                                 = 0x10c1,
    ApiCallId_vkGetPhysicalDeviceMirPresentationSupportKHR                                          = 0x10c2,
    ApiCallId_vkCreateAndroidSurfaceKHR                                                             = 0x10c3,
    ApiCallId_vkCreateWin32SurfaceKHR                                                               = 0x10c4,
    ApiCallId_vkGetPhysicalDeviceWin32PresentationSupportKHR                                        = 0x10c5,
    ApiCallId_vkGetPhysicalDeviceFeatures2KHR                                                       = 0x10c6,
    ApiCallId_vkGetPhysicalDeviceProperties2KHR                                                     = 0x10c7,
    ApiCallId_vkGetPhysicalDeviceFormatProperties2KHR                                               = 0x10c8,
    ApiCallId_vkGetPhysicalDeviceImageFormatProperties2KHR                                          = 0x10c9,
    ApiCallId_vkGetPhysicalDeviceQueueFamilyProperties2KHR                                          = 0x10ca,
    ApiCallId_vkGetPhysicalDeviceMemoryProperties2KHR                                               = 0x10cb,
    ApiCallId_vkGetPhysicalDeviceSparseImageFormatProperties2KHR                                    = 0x10cc,
    ApiCallId_vkGetDeviceGroupPeerMemoryFeaturesKHR                                                 = 0x10cd,
    ApiCallId_vkCmdSetDeviceMaskKHR                                                                 = 0x10ce,
    ApiCallId_vkCmdDispatchBaseKHR                                                                  = 0x10cf,
    ApiCallId_vkTrimCommandPoolKHR                                                                  = 0x10d0,
    ApiCallId_vkEnumeratePhysicalDeviceGroupsKHR                                                    = 0x10d1,
    ApiCallId_vkGetPhysicalDeviceExternalBufferPropertiesKHR                                        = 0x10d2,
    ApiCallId_vkGetMemoryWin32HandleKHR                                                             = 0x10d3,
    ApiCallId_vkGetMemoryWin32HandlePropertiesKHR                                                   = 0x10d4,
    ApiCallId_vkGetMemoryFdKHR                                                                      = 0x10d5,
    ApiCallId_vkGetMemoryFdPropertiesKHR                                                            = 0x10d6,
    ApiCallId_vkGetPhysicalDeviceExternalSemaphorePropertiesKHR                                     = 0x10d7,
    ApiCallId_vkImportSemaphoreWin32HandleKHR                                                       = 0x10d8,
    ApiCallId_vkGetSemaphoreWin32HandleKHR                                                          = 0x10d9,
    ApiCallId_vkImportSemaphoreFdKHR                                                                = 0x10da,
    ApiCallId_vkGetSemaphoreFdKHR                                                                   = 0x10db,
    ApiCallId_vkCmdPushDescriptorSetKHR                                                             = 0x10dc,
    ApiCallId_vkCmdPushDescriptorSetWithTemplateKHR                                                 = 0x10dd,
    ApiCallId_vkCreateDescriptorUpdateTemplateKHR                                                   = 0x10de,
    ApiCallId_vkDestroyDescriptorUpdateTemplateKHR                                                  = 0x10df,
    ApiCallId_vkUpdateDescriptorSetWithTemplateKHR                                                  = 0x10e0,
    ApiCallId_vkGetSwapchainStatusKHR                                                               = 0x10e1,
    ApiCallId_vkGetPhysicalDeviceExternalFencePropertiesKHR                                         = 0x10e2,
    ApiCallId_vkImportFenceWin32HandleKHR                                                           = 0x10e3,
    ApiCallId_vkGetFenceWin32HandleKHR                                                              = 0x10e4,
    ApiCallId_vkImportFenceFdKHR                                                                    = 0x10e5,
    ApiCallId_vkGetFenceFdKHR                                                                       = 0x10e6,
    ApiCallId_vkGetPhysicalDeviceSurfaceCapabilities2KHR                                            = 0x10e7,
    ApiCallId_vkGetPhysicalDeviceSurfaceFormats2KHR                                                 = 0x10e8,
    ApiCallId_vkGetImageMemoryRequirements2KHR                                                      = 0x10e9,
    ApiCallId_vkGetBufferMemoryRequirements2KHR                                                     = 0x10ea,
    ApiCallId_vkGetImageSparseMemoryRequirements2KHR                                                = 0x10eb,
    ApiCallId_vkCreateSamplerYcbcrConversionKHR                                                     = 0x10ec,
    ApiCallId_vkDestroySamplerYcbcrConversionKHR                                                    = 0x10ed,
    ApiCallId_vkBindBufferMemory2KHR                                                                = 0x10ee,
    ApiCallId_vkBindImageMemory2KHR                                                                 = 0x10ef,
    ApiCallId_vkGetDescriptorSetLayoutSupportKHR                                                    = 0x10f0,
    ApiCallId_vkCreateDebugReportCallbackEXT                                                        = 0x10f1,
    ApiCallId_vkDestroyDebugReportCallbackEXT                                                       = 0x10f2,
    ApiCallId_vkDebugReportMessageEXT                                                               = 0x10f3,
    ApiCallId_vkDebugMarkerSetObjectTagEXT                                                          = 0x10f4,
    ApiCallId_vkDebugMarkerSetObjectNameEXT                                                         = 0x10f5,
    ApiCallId_vkCmdDebugMarkerBeginEXT                                                              = 0x10f6,
    ApiCallId_vkCmdDebugMarkerEndEXT                                                                = 0x10f7,
    ApiCallId_vkCmdDebugMarkerInsertEXT                                                             = 0x10f8,
    ApiCallId_vkCmdDrawIndirectCountAMD                                                             = 0x10f9,
    ApiCallId_vkCmdDrawIndexedIndirectCountAMD                                                      = 0x10fa,
    ApiCallId_vkGetShaderInfoAMD                                                                    = 0x10fb,
    ApiCallId_vkGetPhysicalDeviceExternalImageFormatPropertiesNV                                    = 0x10fc,
    ApiCallId_vkGetMemoryWin32HandleNV                                                              = 0x10fd,
    ApiCallId_vkCreateViSurfaceNN                                                                   = 0x10fe,
    ApiCallId_vkCmdProcessCommandsNVX                                                               = 0x10ff,
    ApiCallId_vkCmdReserveSpaceForCommandsNVX                                                       = 0x1100,
    ApiCallId_vkCreateIndirectCommandsLayoutNVX                                                     = 0x1101,
    ApiCallId_vkDestroyIndirectCommandsLayoutNVX                                                    = 0x1102,
    ApiCallId_vkCreateObjectTableNVX                                                                = 0x1103,
    ApiCallId_vkDestroyObjectTableNVX                                                               = 0x1104,
    ApiCallId_vkRegisterObjectsNVX                                                                  = 0x1105,
    ApiCallId_vkUnregisterObjectsNVX                                                                = 0x1106,
    ApiCallId_vkGetPhysicalDeviceGeneratedCommandsPropertiesNVX                                     = 0x1107,
    ApiCallId_vkCmdSetViewportWScalingNV                                                            = 0x1108,
    ApiCallId_vkReleaseDisplayEXT                                                                   = 0x1109,
    ApiCallId_vkAcquireXlibDisplayEXT                                                               = 0x110a,
    ApiCallId_vkGetRandROutputDisplayEXT                                                            = 0x110b,
    ApiCallId_vkGetPhysicalDeviceSurfaceCapabilities2EXT                                            = 0x110c,
    ApiCallId_vkDisplayPowerControlEXT                                                              = 0x110d,
    ApiCallId_vkRegisterDeviceEventEXT                                                              = 0x110e,
    ApiCallId_vkRegisterDisplayEventEXT                                                             = 0x110f,
    ApiCallId_vkGetSwapchainCounterEXT                                                              = 0x1110,
    ApiCallId_vkGetRefreshCycleDurationGOOGLE                                                       = 0x1111,
    ApiCallId_vkGetPastPresentationTimingGOOGLE                                                     = 0x1112,
    ApiCallId_vkCmdSetDiscardRectangleEXT                                                           = 0x1113,
    ApiCallId_vkSetHdrMetadataEXT                                                                   = 0x1114,
    ApiCallId_vkCreateIOSSurfaceMVK                                                                 = 0x1115,
    ApiCallId_vkCreateMacOSSurfaceMVK                                                               = 0x1116,
    ApiCallId_vkSetDebugUtilsObjectNameEXT                                                          = 0x1117,
    ApiCallId_vkSetDebugUtilsObjectTagEXT                                                           = 0x1118,
    ApiCallId_vkQueueBeginDebugUtilsLabelEXT                                                        = 0x1119,
    ApiCallId_vkQueueEndDebugUtilsLabelEXT                                                          = 0x111a,
    ApiCallId_vkQueueInsertDebugUtilsLabelEXT                                                       = 0x111b,
    ApiCallId_vkCmdBeginDebugUtilsLabelEXT                                                          = 0x111c,
    ApiCallId_vkCmdEndDebugUtilsLabelEXT                                                            = 0x111d,
    ApiCallId_vkCmdInsertDebugUtilsLabelEXT                                                         = 0x111e,
    ApiCallId_vkCreateDebugUtilsMessengerEXT                                                        = 0x111f,
    ApiCallId_vkDestroyDebugUtilsMessengerEXT                                                       = 0x1120,
    ApiCallId_vkSubmitDebugUtilsMessageEXT                                                          = 0x1121,
    ApiCallId_vkGetAndroidHardwareBufferPropertiesANDROID                                           = 0x1122,
    ApiCallId_vkGetMemoryAndroidHardwareBufferANDROID                                               = 0x1123,
    ApiCallId_vkCmdSetSampleLocationsEXT                                                            = 0x1124,
    ApiCallId_vkGetPhysicalDeviceMultisamplePropertiesEXT                                           = 0x1125,
    ApiCallId_vkCreateValidationCacheEXT                                                            = 0x1126,
    ApiCallId_vkDestroyValidationCacheEXT                                                           = 0x1127,
    ApiCallId_vkMergeValidationCachesEXT                                                            = 0x1128,
    ApiCallId_vkGetValidationCacheDataEXT                                                           = 0x1129,
    ApiCallId_vkGetMemoryHostPointerPropertiesEXT                                                   = 0x112a,
    ApiCallId_vkCmdWriteBufferMarkerAMD                                                             = 0x112b,
};

BRIMSTONE_END_NAMESPACE(brimstone)
BRIMSTONE_END_NAMESPACE(format)

#endif // BRIMSTONE_FORMAT_API_CALL_ID_H
