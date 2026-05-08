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

#ifndef GFXRECON_FORMAT_FORMAT_ARM_H
#define GFXRECON_FORMAT_FORMAT_ARM_H

#include "format/api_call_id.h"
#include "format/format.h"
#include "format/format_util.h"
#include <cstdint>
#include <type_traits>
#include <any>
#include <unordered_map>

GFXRECON_BEGIN_NAMESPACE(gfxrecon)
GFXRECON_BEGIN_NAMESPACE(format)
GFXRECON_BEGIN_NAMESPACE(arm)
GFXRECON_BEGIN_NAMESPACE(MetaDataType)

using MetaDataTypeUnderlyingType = std::underlying_type<format::MetaDataType>::type;

constexpr format::MetaDataType CreateMetaDataTypeARM(MetaDataTypeUnderlyingType type_id)
{
    constexpr auto kBeginExperimentalReservedRange =
        static_cast<MetaDataTypeUnderlyingType>(format::MetaDataType::kBeginExperimentalReservedRange);
    return static_cast<format::MetaDataType>(kBeginExperimentalReservedRange + type_id);
}

inline constexpr format::MetaDataType kInitTensorCommand                       = CreateMetaDataTypeARM(0);
inline constexpr format::MetaDataType kFixShaderGroupHandleCommand             = CreateMetaDataTypeARM(1);
inline constexpr format::MetaDataType kFixDescriptorDataCommand                = CreateMetaDataTypeARM(2);
inline constexpr format::MetaDataType kFixShadowMemoryCommand                  = CreateMetaDataTypeARM(3);
inline constexpr format::MetaDataType kFillMemoryResourceAddressCommand        = CreateMetaDataTypeARM(4);
inline constexpr format::MetaDataType kGetDx12AccelerationStructureSizeCommand = CreateMetaDataTypeARM(5);
inline constexpr format::MetaDataType kMemoryRequirementsCommand               = CreateMetaDataTypeARM(6);

// Enums used in ARM builds up to r4p1 release that are not reserved upstream
enum class ConflictingMetaDataTypes : MetaDataTypeUnderlyingType
{
    kFixShaderGroupHandleCommand = 35,
    kInitTensorCommand           = 36,
};

GFXRECON_END_NAMESPACE(MetaDataType)

#pragma pack(push)
#pragma pack(4)

// Prevent size_t from being used in data structs that will be written to the capture file.
#define size_t \
    static_assert(false, "Capture file data types must be constant size across all platforms. size_t is not allowed.");

struct GetDx12AccelerationStructureSizeCommandHeader
{
    MetaDataHeader   meta_header{};
    ThreadId         thread_id;
    format::HandleId device_id{ 0 };
    format::HandleId resource_id{ 0 };
    uint64_t         acceleration_structure_address{ 0 };
    uint64_t         num_instance_descs{ 0 };
    uint64_t         data_size{ 0 };

    // In the capture file, accel struct data is written in the following order:
    // GetDx12AccelerationStructureSizeCommandHeader
    // data() { D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUT }
};

struct FillMemoryResourceAddressCommandHeader
{
    MetaDataHeader   meta_header;
    format::ThreadId thread_id;
    uint64_t         resource_address_count;
};

struct Dx12FillMemoryResourceAddressInfo
{
    uint64_t          offset;
    ResourceValueType type;

    // The relevant resource, descriptor, or state object properties.
    format::HandleId object_id;
    // Base GPU VA or GPU Descriptor start address.
    uint64_t start_value;
    // GPU VA or GPU Descriptor found in memory.
    uint64_t adjusted_value;
    // Shader identifier found in memory.
    uint8_t shader_id[kMaxShaderGroupHandleSize];
};

struct ResourceMemoryRequirementsCommandHeader
{
    MetaDataHeader   meta_header;
    format::HandleId device_id;
    uint32_t         resources_count;
    uint64_t         reserved[4];
};

enum class ResourceMemoryRequirementsProperties : uint8_t
{
    kResourceType   = 0,
    kResourceHandle = 1,
    kAliasingGroup  = 2,
    kCreateInfo     = 3
};

enum class ResourceMemoryRequirementsPropertiesResourceType : uint8_t
{
    kResourceTypeVkBuffer = 0,
    kResourceTypeVkImage  = 1,
    kResourceTypeVkTensor = 2,
};

using ResourceMemoryRequirementsInfo = std::unordered_map<ResourceMemoryRequirementsProperties, std::any>;

// Restore size_t to normal behavior.
#undef size_t

#pragma pack(pop)

GFXRECON_END_NAMESPACE(arm)
GFXRECON_END_NAMESPACE(format)
GFXRECON_END_NAMESPACE(gfxrecon)

#endif // GFXRECON_FORMAT_FORMAT_ARM_H
