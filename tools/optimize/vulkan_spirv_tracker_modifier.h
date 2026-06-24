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
#ifndef GFXRECON_TOOLS_OPTIMIZE_VULKAN_SPIRV_TRACK_MODIFIER_H
#define GFXRECON_TOOLS_OPTIMIZE_VULKAN_SPIRV_TRACK_MODIFIER_H

#include <cstdint>
#include <filesystem>
#include <unordered_map>
#include <map>
#include <optional>
#include <unordered_set>
#include <deque>
#include <variant>
#include <vulkan/vulkan_core.h>

#include "decode/api_decoder.h"
#include "format/format.h"
#include "vulkan_spirv_tracker_fixup_location_builder.h"
#include "vulkan_spirv_tracker_provenance_tracker.h"
#include "vulkan_spirv_tracker_types.h"
#include "util/defines.h"
#include "encode/parameter_buffer.h"
#include "util/vulkan_modifier_base.h"
#include "framework/spirv_simulator.hpp"
#include "framework/memory_flag_tracker.hpp"

#include <list>

GFXRECON_BEGIN_NAMESPACE(gfxrecon)
GFXRECON_BEGIN_NAMESPACE(decode)

struct VulkanSpirvTrackerOptions
{
    bool                                 verbose                     = false;
    bool                                 error_on_buffers_incomplete = false;
    std::optional<std::filesystem::path> fixup_json_path             = std::nullopt;
};

// Performs the track of spirv simulator input and output.
class VulkanSpirvTrackModifier : public util::VulkanModifierBase
{
  public:
    explicit VulkanSpirvTrackModifier(const VulkanSpirvTrackerOptions& options = VulkanSpirvTrackerOptions());

    virtual bool CanOptimize() override;

    void FinalizeAnalysis();
    void PrepareForModificationPass();

    virtual void
    ProcessFillMemoryCommand(uint64_t memory_id, uint64_t offset, uint64_t size, const uint8_t* data) override;

    virtual void Process_vkCreateBuffer(const ApiCallInfo&                                   call_info,
                                        VkResult                                             returnValue,
                                        format::HandleId                                     device,
                                        StructPointerDecoder<Decoded_VkBufferCreateInfo>*    pCreateInfo,
                                        StructPointerDecoder<Decoded_VkAllocationCallbacks>* pAllocator,
                                        HandlePointerDecoder<VkBuffer>*                      pBuffer) override;

    virtual void Process_vkDestroyBuffer(const ApiCallInfo&                                   call_info,
                                         format::HandleId                                     device,
                                         format::HandleId                                     buffer,
                                         StructPointerDecoder<Decoded_VkAllocationCallbacks>* pAllocator) override;

    virtual void Process_vkAllocateMemory(const ApiCallInfo&                                   call_info,
                                          VkResult                                             returnValue,
                                          format::HandleId                                     device,
                                          StructPointerDecoder<Decoded_VkMemoryAllocateInfo>*  pAllocateInfo,
                                          StructPointerDecoder<Decoded_VkAllocationCallbacks>* pAllocator,
                                          HandlePointerDecoder<VkDeviceMemory>*                pMemory) override;

    virtual void Process_vkFreeMemory(const ApiCallInfo&                                   call_info,
                                      format::HandleId                                     device,
                                      format::HandleId                                     memory,
                                      StructPointerDecoder<Decoded_VkAllocationCallbacks>* pAllocator) override;

    virtual void Process_vkBindBufferMemory(const ApiCallInfo& call_info,
                                            VkResult           returnValue,
                                            format::HandleId   device,
                                            format::HandleId   buffer,
                                            format::HandleId   memory,
                                            VkDeviceSize       memory_offset) override;

    virtual void Process_vkBindBufferMemory2(const ApiCallInfo&                                    call_info,
                                             VkResult                                              returnValue,
                                             format::HandleId                                      device,
                                             uint32_t                                              bindInfoCount,
                                             StructPointerDecoder<Decoded_VkBindBufferMemoryInfo>* pBindInfos) override;

    virtual void Process_vkMapMemory(const ApiCallInfo&               call_info,
                                     VkResult                         returnValue,
                                     format::HandleId                 device,
                                     format::HandleId                 memory,
                                     VkDeviceSize                     offset,
                                     VkDeviceSize                     size,
                                     VkMemoryMapFlags                 flags,
                                     PointerDecoder<uint64_t, void*>* ppData) override;

    virtual void Process_vkMapMemory2(const ApiCallInfo&                             call_info,
                                      VkResult                                       returnValue,
                                      format::HandleId                               device,
                                      StructPointerDecoder<Decoded_VkMemoryMapInfo>* pMemoryMapInfo,
                                      PointerDecoder<uint64_t, void*>*               ppData) override;

    virtual void Process_vkUnmapMemory2(const ApiCallInfo&                               call_info,
                                        VkResult                                         returnValue,
                                        format::HandleId                                 device,
                                        StructPointerDecoder<Decoded_VkMemoryUnmapInfo>* pMemoryUnmapInfo) override;

    virtual void
    Process_vkUnmapMemory(const ApiCallInfo& call_info, format::HandleId device, format::HandleId memory) override;

    virtual void Process_vkCreateAccelerationStructureKHR(
        const ApiCallInfo&                                                  call_info,
        VkResult                                                            returnValue,
        format::HandleId                                                    device,
        StructPointerDecoder<Decoded_VkAccelerationStructureCreateInfoKHR>* pCreateInfo,
        StructPointerDecoder<Decoded_VkAllocationCallbacks>*                pAllocator,
        HandlePointerDecoder<VkAccelerationStructureKHR>*                   pAccelerationStructure) override;

    virtual void
    Process_vkDestroyAccelerationStructureKHR(const ApiCallInfo& call_info,
                                              format::HandleId   device,
                                              format::HandleId   accelerationStructure,
                                              StructPointerDecoder<Decoded_VkAllocationCallbacks>* pAllocator) override;

    virtual void Process_vkCreateShaderModule(const ApiCallInfo&                                      call_info,
                                              VkResult                                                returnValue,
                                              format::HandleId                                        device,
                                              StructPointerDecoder<Decoded_VkShaderModuleCreateInfo>* pCreateInfo,
                                              StructPointerDecoder<Decoded_VkAllocationCallbacks>*    pAllocator,
                                              HandlePointerDecoder<VkShaderModule>* pShaderModule) override;

    virtual void
    Process_vkCreateDescriptorSetLayout(const ApiCallInfo&                                             call_info,
                                        VkResult                                                       returnValue,
                                        format::HandleId                                               device,
                                        StructPointerDecoder<Decoded_VkDescriptorSetLayoutCreateInfo>* pCreateInfo,
                                        StructPointerDecoder<Decoded_VkAllocationCallbacks>*           pAllocator,
                                        HandlePointerDecoder<VkDescriptorSetLayout>* pSetLayout) override;

    virtual void
    Process_vkDestroyDescriptorSetLayout(const ApiCallInfo&                                   call_info,
                                         format::HandleId                                     device,
                                         format::HandleId                                     descriptorSetLayout,
                                         StructPointerDecoder<Decoded_VkAllocationCallbacks>* pAllocator) override;

    virtual void Process_vkGetDescriptorSetLayoutSizeEXT(const ApiCallInfo&            call_info,
                                                         format::HandleId              device,
                                                         format::HandleId              layout,
                                                         PointerDecoder<VkDeviceSize>* pLayoutSizeInBytes) override;

    virtual void Process_vkGetDescriptorSetLayoutBindingOffsetEXT(const ApiCallInfo&            call_info,
                                                                  format::HandleId              device,
                                                                  format::HandleId              layout,
                                                                  uint32_t                      binding,
                                                                  PointerDecoder<VkDeviceSize>* pOffset) override;

    virtual void Process_vkCreateDescriptorPool(const ApiCallInfo&                                        call_info,
                                                VkResult                                                  returnValue,
                                                format::HandleId                                          device,
                                                StructPointerDecoder<Decoded_VkDescriptorPoolCreateInfo>* pCreateInfo,
                                                StructPointerDecoder<Decoded_VkAllocationCallbacks>*      pAllocator,
                                                HandlePointerDecoder<VkDescriptorPool>* pDescriptorPool) override;

    virtual void
    Process_vkDestroyDescriptorPool(const ApiCallInfo&                                   call_info,
                                    format::HandleId                                     device,
                                    format::HandleId                                     descriptorPool,
                                    StructPointerDecoder<Decoded_VkAllocationCallbacks>* pAllocator) override;

    virtual void
    Process_vkAllocateDescriptorSets(const ApiCallInfo&                                         call_info,
                                     VkResult                                                   returnValue,
                                     format::HandleId                                           device,
                                     StructPointerDecoder<Decoded_VkDescriptorSetAllocateInfo>* pAllocateInfo,
                                     HandlePointerDecoder<VkDescriptorSet>* pDescriptorSets) override;

    virtual void
    Process_vkUpdateDescriptorSets(const ApiCallInfo&                                  call_info,
                                   format::HandleId                                    device,
                                   uint32_t                                            descriptorWriteCount,
                                   StructPointerDecoder<Decoded_VkWriteDescriptorSet>* pDescriptorWrites,
                                   uint32_t                                            descriptorCopyCount,
                                   StructPointerDecoder<Decoded_VkCopyDescriptorSet>*  pDescriptorCopies) override;

    virtual void Process_vkCreatePipelineLayout(const ApiCallInfo&                                        call_info,
                                                VkResult                                                  returnValue,
                                                format::HandleId                                          device,
                                                StructPointerDecoder<Decoded_VkPipelineLayoutCreateInfo>* pCreateInfo,
                                                StructPointerDecoder<Decoded_VkAllocationCallbacks>*      pAllocator,
                                                HandlePointerDecoder<VkPipelineLayout>* pPipelineLayout) override;

    virtual void
    Process_vkDestroyPipelineLayout(const ApiCallInfo&                                   call_info,
                                    format::HandleId                                     device,
                                    format::HandleId                                     pipelineLayout,
                                    StructPointerDecoder<Decoded_VkAllocationCallbacks>* pAllocator) override;

    virtual void
    Process_vkCreateComputePipelines(const ApiCallInfo&                                         call_info,
                                     VkResult                                                   returnValue,
                                     format::HandleId                                           device,
                                     format::HandleId                                           pipelineCache,
                                     uint32_t                                                   createInfoCount,
                                     StructPointerDecoder<Decoded_VkComputePipelineCreateInfo>* pCreateInfos,
                                     StructPointerDecoder<Decoded_VkAllocationCallbacks>*       pAllocator,
                                     HandlePointerDecoder<VkPipeline>*                          pPipelines) override;

    virtual void
    Process_vkCreateGraphicsPipelines(const ApiCallInfo&                                          call_info,
                                      VkResult                                                    returnValue,
                                      format::HandleId                                            device,
                                      format::HandleId                                            pipelineCache,
                                      uint32_t                                                    createInfoCount,
                                      StructPointerDecoder<Decoded_VkGraphicsPipelineCreateInfo>* pCreateInfos,
                                      StructPointerDecoder<Decoded_VkAllocationCallbacks>*        pAllocator,
                                      HandlePointerDecoder<VkPipeline>*                           pPipelines) override;

    virtual void Process_vkDestroyPipeline(const ApiCallInfo&                                   call_info,
                                           format::HandleId                                     device,
                                           format::HandleId                                     pipeline,
                                           StructPointerDecoder<Decoded_VkAllocationCallbacks>* pAllocator) override;

    virtual void
    Process_vkAllocateCommandBuffers(const ApiCallInfo&                                         call_info,
                                     VkResult                                                   returnValue,
                                     format::HandleId                                           device,
                                     StructPointerDecoder<Decoded_VkCommandBufferAllocateInfo>* pAllocateInfo,
                                     HandlePointerDecoder<VkCommandBuffer>* pCommandBuffers) override;

    virtual void Process_vkFreeCommandBuffers(const ApiCallInfo&                     call_info,
                                              format::HandleId                       device,
                                              format::HandleId                       commandPool,
                                              uint32_t                               commandBufferCount,
                                              HandlePointerDecoder<VkCommandBuffer>* pCommandBuffers) override;

    virtual void Process_vkResetCommandBuffer(const ApiCallInfo&        call_info,
                                              VkResult                  returnValue,
                                              format::HandleId          commandBuffer,
                                              VkCommandBufferResetFlags flags) override;

    virtual void
    Process_vkBeginCommandBuffer(const ApiCallInfo&                                      call_info,
                                 VkResult                                                returnValue,
                                 format::HandleId                                        commandBuffer,
                                 StructPointerDecoder<Decoded_VkCommandBufferBeginInfo>* pBeginInfo) override;

    virtual void Process_vkEndCommandBuffer(const ApiCallInfo& call_info,
                                            VkResult           returnValue,
                                            format::HandleId   commandBuffer) override;

    virtual void
    Process_vkGetBufferDeviceAddress(const ApiCallInfo&                                       call_info,
                                     VkDeviceAddress                                          returnValue,
                                     format::HandleId                                         device,
                                     StructPointerDecoder<Decoded_VkBufferDeviceAddressInfo>* pInfo) override;

    virtual void
    Process_vkGetBufferDeviceAddressKHR(const ApiCallInfo&                                       call_info,
                                        VkDeviceAddress                                          returnValue,
                                        format::HandleId                                         device,
                                        StructPointerDecoder<Decoded_VkBufferDeviceAddressInfo>* pInfo) override;

    virtual void
    Process_vkGetBufferDeviceAddressEXT(const ApiCallInfo&                                       call_info,
                                        VkDeviceAddress                                          returnValue,
                                        format::HandleId                                         device,
                                        StructPointerDecoder<Decoded_VkBufferDeviceAddressInfo>* pInfo) override;

    virtual void Process_vkGetAccelerationStructureDeviceAddressKHR(
        const ApiCallInfo&                                                         call_info,
        VkDeviceAddress                                                            returnValue,
        format::HandleId                                                           device,
        StructPointerDecoder<Decoded_VkAccelerationStructureDeviceAddressInfoKHR>* pInfo) override;

    virtual void Process_vkCmdBindPipeline(const ApiCallInfo&  call_info,
                                           format::HandleId    commandBuffer,
                                           VkPipelineBindPoint pipelineBindPoint,
                                           format::HandleId    pipeline) override;

    virtual void Process_vkCmdBindDescriptorSets(const ApiCallInfo&                     call_info,
                                                 format::HandleId                       commandBuffer,
                                                 VkPipelineBindPoint                    pipelineBindPoint,
                                                 format::HandleId                       layout,
                                                 uint32_t                               firstSet,
                                                 uint32_t                               descriptorSetCount,
                                                 HandlePointerDecoder<VkDescriptorSet>* pDescriptorSets,
                                                 uint32_t                               dynamicOffsetCount,
                                                 PointerDecoder<uint32_t>*              pDynamicOffsets) override;

    virtual void Process_vkCmdBindDescriptorBuffersEXT(
        const ApiCallInfo&                                              call_info,
        format::HandleId                                                commandBuffer,
        uint32_t                                                        bufferCount,
        StructPointerDecoder<Decoded_VkDescriptorBufferBindingInfoEXT>* pBindingInfos) override;

    virtual void Process_vkCmdSetDescriptorBufferOffsetsEXT(const ApiCallInfo&            call_info,
                                                            format::HandleId              commandBuffer,
                                                            VkPipelineBindPoint           pipelineBindPoint,
                                                            format::HandleId              layout,
                                                            uint32_t                      firstSet,
                                                            uint32_t                      setCount,
                                                            PointerDecoder<uint32_t>*     pBufferIndices,
                                                            PointerDecoder<VkDeviceSize>* pOffsets) override;

    virtual void Process_vkCmdPushConstants(const ApiCallInfo&       call_info,
                                            format::HandleId         commandBuffer,
                                            format::HandleId         layout,
                                            VkShaderStageFlags       stageFlags,
                                            uint32_t                 offset,
                                            uint32_t                 size,
                                            PointerDecoder<uint8_t>* pValues) override;

    virtual void Process_vkCmdFillBuffer(const ApiCallInfo& call_info,
                                         format::HandleId   commandBuffer,
                                         format::HandleId   dstBuffer,
                                         VkDeviceSize       dstOffset,
                                         VkDeviceSize       size,
                                         uint32_t           data) override;

    virtual void Process_vkCmdUpdateBuffer(const ApiCallInfo&       call_info,
                                           format::HandleId         commandBuffer,
                                           format::HandleId         dstBuffer,
                                           VkDeviceSize             dstOffset,
                                           VkDeviceSize             dataSize,
                                           PointerDecoder<uint8_t>* pData) override;

    virtual void Process_vkCmdCopyBuffer(const ApiCallInfo&                          call_info,
                                         format::HandleId                            commandBuffer,
                                         format::HandleId                            srcBuffer,
                                         format::HandleId                            dstBuffer,
                                         uint32_t                                    regionCount,
                                         StructPointerDecoder<Decoded_VkBufferCopy>* pRegions) override;

    virtual void Process_vkCmdCopyBuffer2(const ApiCallInfo&                               call_info,
                                          format::HandleId                                 commandBuffer,
                                          StructPointerDecoder<Decoded_VkCopyBufferInfo2>* pCopyBufferInfo) override;

    virtual void Process_vkCmdCopyBuffer2KHR(const ApiCallInfo&                               call_info,
                                             format::HandleId                                 commandBuffer,
                                             StructPointerDecoder<Decoded_VkCopyBufferInfo2>* pCopyBufferInfo) override;

    virtual void Process_vkCmdBuildAccelerationStructuresKHR(
        const ApiCallInfo&                                                         call_info,
        format::HandleId                                                           commandBuffer,
        uint32_t                                                                   infoCount,
        StructPointerDecoder<Decoded_VkAccelerationStructureBuildGeometryInfoKHR>* pInfos,
        StructPointerDecoder<Decoded_VkAccelerationStructureBuildRangeInfoKHR*>*   ppBuildRangeInfos) override;

    virtual void Process_vkCmdDispatch(const ApiCallInfo& call_info,
                                       format::HandleId   commandBuffer,
                                       uint32_t           groupCountX,
                                       uint32_t           groupCountY,
                                       uint32_t           groupCountZ) override;

    virtual void Process_vkCmdDispatchIndirect(const ApiCallInfo& call_info,
                                               format::HandleId   commandBuffer,
                                               format::HandleId   buffer,
                                               VkDeviceSize       offset) override;

    virtual void Process_vkCmdDraw(const ApiCallInfo& call_info,
                                   format::HandleId   commandBuffer,
                                   uint32_t           vertexCount,
                                   uint32_t           instanceCount,
                                   uint32_t           firstVertex,
                                   uint32_t           firstInstance) override;

    virtual void Process_vkCmdDrawIndexed(const ApiCallInfo& call_info,
                                          format::HandleId   commandBuffer,
                                          uint32_t           indexCount,
                                          uint32_t           instanceCount,
                                          uint32_t           firstIndex,
                                          int32_t            vertexOffset,
                                          uint32_t           firstInstance) override;

    virtual void Process_vkCmdDrawIndirect(const ApiCallInfo& call_info,
                                           format::HandleId   commandBuffer,
                                           format::HandleId   buffer,
                                           VkDeviceSize       offset,
                                           uint32_t           drawCount,
                                           uint32_t           stride) override;

    virtual void Process_vkCmdDrawIndexedIndirect(const ApiCallInfo& call_info,
                                                  format::HandleId   commandBuffer,
                                                  format::HandleId   buffer,
                                                  VkDeviceSize       offset,
                                                  uint32_t           drawCount,
                                                  uint32_t           stride) override;

    virtual void Process_vkQueueSubmit(const ApiCallInfo&                          call_info,
                                       VkResult                                    returnValue,
                                       format::HandleId                            queue,
                                       uint32_t                                    submitCount,
                                       StructPointerDecoder<Decoded_VkSubmitInfo>* pSubmits,
                                       format::HandleId                            fence) override;

    virtual void Process_vkQueueSubmit2(const ApiCallInfo&                           call_info,
                                        VkResult                                     returnValue,
                                        format::HandleId                             queue,
                                        uint32_t                                     submitCount,
                                        StructPointerDecoder<Decoded_VkSubmitInfo2>* pSubmits,
                                        format::HandleId                             fence) override;

    virtual void Process_vkQueueSubmit2KHR(const ApiCallInfo&                           call_info,
                                           VkResult                                     returnValue,
                                           format::HandleId                             queue,
                                           uint32_t                                     submitCount,
                                           StructPointerDecoder<Decoded_VkSubmitInfo2>* pSubmits,
                                           format::HandleId                             fence) override;

  private:
    void process_SubmitInfo(uint64_t                             submitIndex,
                            const std::vector<format::HandleId>& commandBuffers,
                            const std::vector<format::HandleId>& waitSems     = {},
                            const std::vector<format::HandleId>& signalSems   = {},
                            const std::vector<uint64_t>&         waitValues   = {},
                            const std::vector<uint64_t>&         signalValues = {});

    void signalSemaphoresFrom(uint64_t submitIndex);

    void executeCommandBuffer(format::HandleId commandBuffer_id);
    void executeDispatchDraw(format::HandleId commandBuffer_id, VkPipelineBindPoint bindPoint);
    void outputSimulator(const SPIRVSimulator::SimulationResults& results);
    void resetRecording(format::HandleId commandBuffer);

  private:
    struct ObjectInfo
    {
        format::HandleId handle;
        uint64_t         creation_index;
        uint64_t         destruction_index;
    };

    struct BufferInfo : public ObjectInfo
    {
        uint64_t             size;
        VkBufferUsageFlags   usage;
        VkBufferCreateFlags  flags;
        VkDeviceAddress      device_address;
        std::vector<uint8_t> data;
    };

    struct MemoryBindingRecord
    {
        format::HandleId handle;
        bool             isBuffer;
        VkDeviceSize     offset;
    };

    struct MemoryMapping
    {
        VkDeviceSize offset;
        VkDeviceSize size;
        uint64_t     map_memory;
        uint64_t     shadow_memory;
    };

    struct DeviceMemoryInfo : public ObjectInfo
    {
        uint64_t                         size;
        uint32_t                         type_index;
        VkMemoryAllocateFlags            flags;
        MemoryMapping                    mapping;
        std::vector<MemoryBindingRecord> memory_binding_records_;
    };

    enum class CommandBufferLifeCycle
    {
        Inital = 0,
        Recording,
        Executable,
        Pending,
        Invalid
    };
    struct CommandBufferInfo : public ObjectInfo
    {
        format::HandleId       device_id = 0;
        CommandBufferLifeCycle state;
    };

    struct AccelerationStructureInfo : public ObjectInfo
    {
        format::HandleId               buf_handle;
        uint64_t                       size;
        uint64_t                       offset;
        VkAccelerationStructureTypeKHR type;
        VkDeviceAddress                device_address;
    };

    struct ShaderModuleInfo : public ObjectInfo
    {
        size_t                codeSize;
        std::vector<uint32_t> pCode;
    };

    struct Binding
    {
        uint32_t binding;
        // Declared descriptor type from VkDescriptorSetLayoutBinding::descriptorType.
        // For VK_DESCRIPTOR_TYPE_MUTABLE_EXT, the runtime active descriptor type is tracked
        // per descriptor in DescriptorEntry::type.
        VkDescriptorType              type;
        std::vector<VkDescriptorType> mutable_descriptor_types;
        uint32_t                      descriptorCount;
        VkShaderStageFlags            stageFlags;
        VkDescriptorBindingFlagsEXT   binding_flags;
        // Binding offset for descriptor_buffer_bit flag. Unused otherwise.
        VkDeviceSize offset;
    };

    struct SetLayoutInfo : public ObjectInfo
    {
        VkDescriptorSetLayoutCreateFlags flags;
        // binding num -> binding info
        std::map<uint32_t, Binding> bindings;
        // descriptorSetLayout size when creating for descriptor buffer. Unused otherwise.
        VkDeviceSize size;
    };

    struct DescriptorPoolInfo : public ObjectInfo
    {
        std::vector<format::HandleId> descriptorSets;
    };

    struct DescriptorEntry
    {
        VkDescriptorType type;
        union
        {
            struct
            {
                format::HandleId handle;
                VkDeviceSize     range;
                VkDeviceSize     offset;
            } buffer;
            struct
            {
                format::HandleId handle;
            } acceleration;
        };
    };

    struct DescriptorArray : public Binding
    {
        // array index of descriptors -> descriptor entry
        std::map<uint32_t, DescriptorEntry> descriptor_entries;
        // Inline uniform block bindings are byte-addressed, not descriptor-entry-addressed.
        std::vector<uint8_t> inline_uniform_block_data;
        std::vector<uint8_t> inline_uniform_block_written;
    };

    struct DescriptorSetInfo : public ObjectInfo
    {
        // binding -> descriptors updated on the binding
        std::map<uint32_t, DescriptorArray> binding_descriptor_array;
    };

    struct DynamicBindingRef
    {
        uint32_t         set;
        uint32_t         binding;
        uint32_t         start_index;
        uint32_t         elementCount;
        VkDescriptorType type;
    };

    struct PipelineLayoutInfo : public ObjectInfo
    {
        VkPipelineLayoutCreateFlags flags = 0;
        // index:set number -> setLayout Id
        std::vector<format::HandleId> setLayouts;
        // set number -> dynamic Ref
        std::unordered_map<uint32_t, std::vector<DynamicBindingRef>> dynamicBindingRef;

        std::vector<VkPushConstantRange> pushConstantRanges;
        struct Range
        {
            uint32_t offset;
            uint32_t size;
        };
        std::unordered_map<VkShaderStageFlagBits, std::vector<uint32_t>> rangeIndicesPerStage;
        std::unordered_map<VkShaderStageFlagBits, Range>                 mergedRangePerStage;
    };

    struct ShaderStage
    {
        std::string                          pName;
        format::HandleId                     module;
        VkShaderStageFlagBits                stageFlagBit;
        size_t                               specialization_dataSize;
        std::vector<uint8_t>                 specialization_constant;
        std::unordered_map<uint32_t, size_t> specialization_offset_entries;
    };

    struct PipelineInfo : public ObjectInfo
    {
        format::HandleId         layout;
        std::vector<ShaderStage> stages;
    };

    // used for BindDescriptorBufferEXT
    struct DescriptorBufferBindingInfo
    {
        format::HandleId    handle;       // descriptor buffer handle
        VkDeviceAddress     base_address; // base address of buffer
        VkDeviceAddress     address;      // bound address within buffer
        VkBufferUsageFlags  usage;
        VkBufferUsageFlags2 usage2;
        bool                resolved; // whether the bound address was resolved to a tracked buffer
    };

    // used for SetDescriptorBufferOffsetEXT()
    struct DescriptorBufferOffsetMap
    {
        uint32_t         set;
        uint32_t         buffer_index; // the index of descriptor buffer array in CmdBindDescriptorBuffers
        VkDeviceSize     offset;
        format::HandleId layout; // pipeline layout
    };

    struct DescriptorBufferOffset
    {
        format::HandleId handle;       // descriptor buffer handle
        VkDeviceAddress  base_address; // base address of buffer
        VkDeviceAddress  address;      // bound address within buffer
        VkDeviceSize     offset;
        format::HandleId layout; // pipeline layout
    };

    enum class SourceType
    {
        HostMemory = 0,
        CopyBuffer,
        Update,
        Fill,
        Dispatch,
        Unknow
    };

    struct BufferWriteEvent
    {
        SourceType                sourceType = SourceType::Unknow;
        format::HandleId          buffer;
        format::HandleId          srcBuffer;
        std::vector<uint8_t>      srcPointer;
        std::vector<VkBufferCopy> regions;
        // Trace call index for execute-time provenance paths. UINT64_MAX means
        // this recorded write does not participate in provenance updates.
        uint64_t call_index = UINT64_MAX;

        BufferWriteEvent(SourceType in_source_type, format::HandleId in_buffer, format::HandleId in_src_buffer = 0) :
            sourceType(in_source_type), buffer(in_buffer), srcBuffer(in_src_buffer)
        {}
    };

    // One recorded BuildAS action for a single info_index within one vkCmdBuildAccelerationStructuresKHR call.
    // Recording time only resolves which accelerationStructureReference slots will be consumed;
    // slot values are read and verified later at execute time.
    struct BuildAccelerationStructureAction
    {
        uint64_t                      call_index;
        uint32_t                      info_index;
        format::HandleId              dst_as;
        std::vector<BufferUseSiteKey> candidate_use_sites;
    };

    using ActionCommand = std::variant<BufferWriteEvent, BuildAccelerationStructureAction>;

    struct PushConstantData
    {
        uint32_t             offset;
        uint32_t             size;
        VkShaderStageFlags   stageFlags;
        format::HandleId     layout;
        std::vector<uint8_t> pValues;
        // Stable trace call index of the originating vkCmdPushConstants.
        uint64_t call_index;
    };

    struct DescriptorSetMap
    {
        uint32_t         set;
        format::HandleId descriptor_set;
        format::HandleId layout;
    };

    struct BoundDescriptorSet
    {
        format::HandleId descriptor_set;
        format::HandleId layout;
    };

    struct DynamicBindingOffsetMap
    {
        uint32_t set;
        // binding -> offsets array, ordered map
        std::map<uint32_t, std::vector<uint32_t>> binding_offsets;
    };

    struct DescriptorStateCommand
    {
        enum class Type
        {
            BindDescriptorSets,
            BindDescriptorBuffers,
            SetDescriptorBufferOffsets,
        };

        Type                type       = Type::BindDescriptorSets;
        VkPipelineBindPoint bind_point = VK_PIPELINE_BIND_POINT_MAX_ENUM;

        // used only for BindDescriptorSets type
        std::vector<DescriptorSetMap> descriptor_sets;
        // set -> DynamicBindingOffsetMap
        std::unordered_map<uint32_t, DynamicBindingOffsetMap> dynamic_offsets;
        // used only for BindDescriptorBuffers type
        std::vector<DescriptorBufferBindingInfo> descriptor_buffers;
        // used only for SetDescriptorBufferOffsets
        std::vector<DescriptorBufferOffsetMap> descriptor_buffer_offsets;
    };

    struct CommandBufferRecording
    {
        format::HandleId    command_buffer = 0;
        VkPipelineBindPoint bind_point     = VK_PIPELINE_BIND_POINT_MAX_ENUM;
        bool                in_operation   = false;

        std::vector<PushConstantData> push_constants;

        // bind point -> pipeline handle
        std::unordered_map<VkPipelineBindPoint, format::HandleId> pipelines;
        // ordered descriptor state commands. Preserve original recording order.
        std::vector<DescriptorStateCommand> descriptor_state_commands;

        // Ordered action list used to replay write/build commands in original
        // recording order during execute-time replay.
        std::vector<ActionCommand> action_command_list;

        ////////////////////////////////////
        // index of bound descriptor buffers -> valid or not
        // invalidate the offset once rebinding the descriptor buffer
        std::unordered_map<uint32_t, bool> descriptor_offset_valid;
    };

    enum class DescriptorBackendMode
    {
        NoneBackend = 0,
        DescriptorSet,
        DescriptorBuffer,
        DescriptorHeap,
    };

    struct BindPointState
    {
        format::HandleId pipeline;
        format::HandleId pipeline_layout; // could be 0 if using descriptor heap

        DescriptorBackendMode descriptor_backend_mode = DescriptorBackendMode::NoneBackend;

        // set num -> bound descriptor set and the pipeline layout used to bind it
        // could be null map if descriptor buffer,descriptor heap in using
        std::unordered_map<uint32_t, BoundDescriptorSet> descriptor_set_map;
        // dynamic offsets
        std::vector<uint32_t> dynamic_offsets;
        // set num -> array of dynamic offsets. Auxiliary function
        std::map<uint32_t, std::vector<uint32_t>> dynamic_offsets_perSet;
        // set num -> binding -> dynamic offset count, used to check compatibility
        std::unordered_map<uint32_t, std::unordered_map<uint32_t, uint32_t>> dynamic_offsets_count;

        // set num -> DescriptorBufferOffset
        std::unordered_map<uint32_t, DescriptorBufferOffset> descriptor_buffer_set_offset_map;
    };

    struct CommandBufferState
    {
        format::HandleId                                        command_buffer;
        std::vector<uint8_t>                                    push_constant;
        std::vector<DescriptorBufferBindingInfo>                descriptor_buffers;
        std::unordered_map<VkPipelineBindPoint, BindPointState> bind_point_state;
    };

    // Semaphore state tracker
    struct BinarySemaphoreState
    {
        bool                 signaled         = false;
        uint32_t             signaledBySubmit = 0;
        std::deque<uint64_t> waitingSubmits; // submit indices in submission order
    };
    struct TimelineSemaphoreState
    {
        uint64_t currentValue = 0;
        // pair<submit index, requiredValue>
        std::deque<std::pair<uint64_t, uint64_t>> waitingSubmits;
    };
    struct SemaphoreState
    {
        VkSemaphoreType                                            type;
        std::variant<BinarySemaphoreState, TimelineSemaphoreState> state;
        SemaphoreState() : type(VK_SEMAPHORE_TYPE_BINARY), state(BinarySemaphoreState{}) {}
    };

    // Per-submit metadata
    struct SubmitInfo
    {
        uint32_t submit_index; // API sequence index

        std::vector<format::HandleId> command_buffers;
        std::vector<format::HandleId> wait_semaphores;
        std::vector<format::HandleId> signal_semaphores;

        std::vector<uint64_t> wait_values;   // timeline values (same index as wait_semaphores)
        std::vector<uint64_t> signal_values; // timeline values (same index as signal_semaphores)

        bool submitted = false;
        bool ready     = false;
    };

    // One overlap between a FillMemoryCommand payload slice and a bound buffer slice.
    struct FillMemoryBufferOverlap
    {
        format::HandleId buffer;
        VkDeviceSize     offset_in_buffer;
        uint64_t         offset_in_data;
        uint64_t         size;
    };

    // Fully normalized descriptor payload block returned by the descriptor-set path
    // before the payload pointer is handed to the simulator.
    struct ResolvedDescriptorPayloadBlock
    {
        void*            binding_ptr;
        format::HandleId buffer;
        VkDeviceSize     block_base_offset_in_buffer;
        VkDeviceSize     block_size;
    };

    // Host-side block metadata recorded while wiring concrete descriptor payload
    // blocks into sim_data.bindings for one dispatch/draw execution.
    struct DispatchBlockInfo
    {
        format::HandleId buffer;
        VkDeviceSize     block_base_offset_in_buffer;
        VkDeviceSize     block_size;
    };

    // map key is host-side base pointer of a concrete descriptor payload block wired into sim_data.bindings.
    // Used to map simulator-reported source_ptr back to buffer-relative block metadata during use-site normalization.
    using DispatchBlockInfos = std::unordered_map<const void*, DispatchBlockInfo>;

    struct DispatchPushConstantBlockInfo
    {
        format::HandleId command_buffer                      = format::kNullHandleId;
        const void*      block_ptr                           = nullptr;
        VkDeviceSize     block_base_offset_in_push_constants = 0;
        VkDeviceSize     block_size                          = 0;
    };

    struct UnsupportedDispatchBufferAddressUseSite
    {
        SPIRVSimulator::PhysicalAddressData           simulator_use_site;
        UnsupportedDispatchBufferAddressUseSiteReason reason =
            UnsupportedDispatchBufferAddressUseSiteReason::UnsupportedBitComponentCount;
    };

  private:
    // handle id -> obj info
    std::unordered_map<format::HandleId, BufferInfo>                buffer_entries_;
    std::unordered_map<format::HandleId, DeviceMemoryInfo>          memory_binding_entries_;
    std::unordered_map<format::HandleId, CommandBufferInfo>         command_buffer_entries_;
    std::unordered_map<format::HandleId, AccelerationStructureInfo> acceleration_structure_entries_;
    std::unordered_map<format::HandleId, PipelineInfo>              pipeline_entries_;
    std::unordered_map<format::HandleId, PipelineLayoutInfo>        pipeline_layout_entries_;
    std::unordered_map<format::HandleId, ShaderModuleInfo>          shader_module_entries_;
    std::unordered_map<format::HandleId, SetLayoutInfo>             set_layout_entries_;
    std::unordered_map<format::HandleId, DescriptorPoolInfo>        descriptor_pool_entries_;
    std::unordered_map<format::HandleId, DescriptorSetInfo>         descriptor_set_entries_;

    // buffer device address -> buffer handle
    std::unordered_map<VkDeviceAddress, format::HandleId> buffer_device_addresses_;

    // acceleration structure device address -> as handle
    std::unordered_map<VkDeviceAddress, std::unordered_set<format::HandleId>> acceleration_structure_device_addresses_;

    std::unordered_map<format::HandleId, CommandBufferRecording> command_buffer_recording;

    // command buffer handle -> recording status per dispatch/draw call
    std::unordered_map<format::HandleId, std::vector<CommandBufferRecording>> command_buffer_submit_recordings;

    // command buffer handle -> executing state of command buffer
    std::unordered_map<format::HandleId, CommandBufferState> command_buffer_state;

    // Pass-local allocator state for traced FillMemoryCommand serial ids.
    uint64_t next_fill_memory_serial_id_ = 0;

    uint64_t global_submit_index = 0;

    // global unique submit index -> submit info
    std::unordered_map<uint64_t, SubmitInfo>             submit_entries_;
    std::unordered_map<format::HandleId, SemaphoreState> semaphore_state;
    std::deque<uint64_t>                                 ready_submits; // submit index

    ProvenanceTracker provenance_tracker_;

    // BuildAS use-sites whose current slot value was validated successfully;
    // rejected candidate use-sites are kept for diagnostics.
    std::vector<VerifiedBuildAsUseSite> verified_build_as_use_sites_;
    std::vector<RejectedBuildAsUseSite> rejected_build_as_use_sites_;

    // Verified BuildAS use-sites that were resolved to one root source;
    // unresolved entries are kept when no single root source covers the queried range.
    std::vector<ResolvedBuildAsUseSite>   resolved_build_as_use_sites_;
    std::vector<UnresolvedBuildAsUseSite> unresolved_build_as_use_sites_;

    std::vector<UnsupportedDispatchBufferAddressUseSite> unsupported_dispatch_buffer_address_use_sites_;
    std::vector<VerifiedDispatchBufferAddressUseSite>    verified_dispatch_buffer_address_use_sites_;
    std::vector<RejectedDispatchBufferAddressUseSite>    rejected_dispatch_buffer_address_use_sites_;
    std::vector<ResolvedDispatchBufferAddressUseSite>    resolved_dispatch_buffer_address_use_sites_;
    std::vector<UnresolvedDispatchBufferAddressUseSite>  unresolved_dispatch_buffer_address_use_sites_;

    FixupLocationBuilder fixup_location_builder_;

    // FinalizeAnalysis() runs once per modifier instance after analysis completes.
    bool analysis_finalized_ = false;

    SPIRVSimulator::MemoryFlagTracker mem_flag_tracker;

    // for internal debug
    uint64_t                  global_draw_index = 0;
    VulkanSpirvTrackerOptions options_{};
    bool                      m_verbose = false;
    uint64_t                  m_flags   = 0;

  private:
    void ApplyActionCommands(const CommandBufferRecording& recording);
    void ApplyDescriptorStateCommands(format::HandleId commandBuffer, const CommandBufferRecording& recording);
    bool IsPipelineLayoutCompatibleForSet(format::HandleId lhs, format::HandleId rhs, uint32_t set) const;

    // Convert one FillMemoryCommand into buffer-relative overlap slices.
    std::vector<FillMemoryBufferOverlap>
    CollectFillMemoryBufferOverlaps(uint64_t memory_id, uint64_t fill_offset, uint64_t fill_size) const;

    void ApplyBufferWriteAction(const BufferWriteEvent& event);
    void ApplyBuildAccelerationStructureAction(const BuildAccelerationStructureAction& action);

    void
    ApplyDispatchPhysicalAddressResults(const SPIRVSimulator::SimulationResults&            results,
                                        const DispatchBlockInfos&                           block_infos,
                                        const std::optional<DispatchPushConstantBlockInfo>& push_constant_block_info);

    bool NormalizeDispatchUseSite(const SPIRVSimulator::PhysicalAddressData&          simulator_use_site,
                                  const DispatchBlockInfos&                           block_infos,
                                  const std::optional<DispatchPushConstantBlockInfo>& push_constant_block_info,
                                  DispatchUseSiteKey&                                 out_use_site,
                                  UnsupportedDispatchBufferAddressUseSiteReason&      reason) const;

    bool VerifyDispatchBufferDeviceAddress(VkDeviceAddress                     raw_pointer_value,
                                           format::HandleId&                   out_referenced_buffer,
                                           VkDeviceAddress&                    out_referenced_buffer_base_address,
                                           VkDeviceSize&                       out_referenced_buffer_size,
                                           RejectedDeviceAddressUseSiteReason& reject_reason) const;

    bool VerifyLiveAccelerationStructureDeviceAddress(VkDeviceAddress                     address,
                                                      VkAccelerationStructureTypeKHR      expected_type,
                                                      uint64_t                            build_call_index,
                                                      format::HandleId&                   referenced_as,
                                                      VkDeviceAddress&                    referenced_as_base_address,
                                                      VkDeviceSize&                       referenced_as_size,
                                                      RejectedDeviceAddressUseSiteReason& reject_reason) const;
    // Dispatch use-site multiplexer over buffer-backed and push-constant-backed sources.
    bool ResolveDispatchUseSiteToRoot(const DispatchUseSiteKey&             use_site,
                                      ResolvedRootRange&                    out,
                                      UnresolvedDeviceAddressUseSiteReason& reason) const;
    // Bridge all resolved device-address use-sites to the shared pure-data
    // inputs consumed later by the fixup-location builder.
    std::vector<DeviceAddressFixupInput> BuildFixupInputs() const;

    void WriteFixDeviceAddressCmd(format::HandleId                                relation_id,
                                  const std::vector<format::AddressLocationInfo>& address_locations);

    using BufferAddressMap = std::unordered_map<VkDeviceAddress, format::HandleId>;

    inline BufferAddressMap::const_iterator findEntryFromBufferDeviceAddress(VkDeviceAddress address) const
    {
        return std::find_if(
            buffer_device_addresses_.begin(), buffer_device_addresses_.end(), [address, this](const auto& entry) {
                auto buffer_iter = buffer_entries_.find(entry.second);
                if (buffer_iter == buffer_entries_.end())
                {
                    return false;
                }
                const auto& buffer_info = buffer_iter->second;
                return (address >= entry.first) && (address < entry.first + buffer_info.size);
            });
    }
};

GFXRECON_END_NAMESPACE(decode)
GFXRECON_END_NAMESPACE(gfxrecon)

#endif // GFXRECON_TOOLS_OPTIMIZE_VULKAN_SPIRV_TRACK_MODIFIER_H