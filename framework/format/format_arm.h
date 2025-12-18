#ifndef GFXRECON_FORMAT_FORMAT_ARM_H
#define GFXRECON_FORMAT_FORMAT_ARM_H

#include "format/api_call_id.h"
#include "format/format.h"
#include "format/format_util.h"
#include <cstdint>
#include <type_traits>

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

// Enums used in ARM builds up to r4p1 release that are not reserved upstream
enum class ConflictingMetaDataTypes : MetaDataTypeUnderlyingType
{
    kFixShaderGroupHandleCommand = 35,
    kInitTensorCommand           = 36,
};

inline bool IsConflictingMetadata(const format::FileHeader& trace_header, format::MetaDataType input_type)
{
    if (trace_header.major_version == 0 && trace_header.minor_version == 0)
    {
        if (input_type == static_cast<format::MetaDataType>(ConflictingMetaDataTypes::kInitTensorCommand) ||
            input_type == static_cast<format::MetaDataType>(ConflictingMetaDataTypes::kFixShaderGroupHandleCommand))
        {
            return true;
        }
    }
    return false;
}

// Conversion from trace metadata id to replay metadata id
inline MetaDataId GetVersionedMetaDataId(const format::FileHeader& trace_header, MetaDataId meta_data_id)
{
    ApiFamilyId          api_family = GetMetaDataApi(meta_data_id);
    format::MetaDataType input_type = GetMetaDataType(meta_data_id);

    if (!IsConflictingMetadata(trace_header, input_type))
    {
        return meta_data_id;
    }

    format::MetaDataType output_type = input_type;

    ConflictingMetaDataTypes original_type = static_cast<ConflictingMetaDataTypes>(input_type);
    switch (original_type)
    {
        case ConflictingMetaDataTypes::kInitTensorCommand:
            output_type = arm::MetaDataType::kInitTensorCommand;
            break;
        case ConflictingMetaDataTypes::kFixShaderGroupHandleCommand:
            output_type = arm::MetaDataType::kFixShaderGroupHandleCommand;
            break;
    }
    return MakeMetaDataId(api_family, output_type);
}

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

// Restore size_t to normal behavior.
#undef size_t

#pragma pack(pop)

GFXRECON_END_NAMESPACE(arm)
GFXRECON_END_NAMESPACE(format)
GFXRECON_END_NAMESPACE(gfxrecon)

#endif // GFXRECON_FORMAT_FORMAT_ARM_H
