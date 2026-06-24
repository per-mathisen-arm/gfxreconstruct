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
#ifndef GFXRECON_TOOLS_OPTIMIZE_VULKAN_SPIRV_TRACKER_TYPES_H
#define GFXRECON_TOOLS_OPTIMIZE_VULKAN_SPIRV_TRACKER_TYPES_H

#include <cstdint>
#include <unordered_map>
#include <variant>
#include <vector>

#include <vulkan/vulkan_core.h>

#include "format/format.h"
#include "util/defines.h"

GFXRECON_BEGIN_NAMESPACE(gfxrecon)
GFXRECON_BEGIN_NAMESPACE(decode)

// Distinguishes which API path originally produced the root payload bytes.
enum class ProvenanceRootType
{
    FillMemory,
    UpdateBuffer,
    PushConstants,
};

// Identifies a byte range inside the payload of one root command.
// source_index is fill_serial_id for FillMemory, and ApiCallInfo.index for
// vkCmdUpdateBuffer / vkCmdPushConstants.
struct ProvenanceRootKey
{
    uint64_t source_index   = 0;
    uint64_t payload_offset = 0;
};

// One flattened provenance slice: bytes [offset, offset + size) currently
// map to a continuous payload slice of one root source.
struct ProvenanceSpan
{
    uint64_t           offset = 0;
    uint64_t           size   = 0;
    ProvenanceRootType type   = ProvenanceRootType::FillMemory;
    ProvenanceRootKey  root   = {};
};

// Ordered, non-overlapping flattened provenance spans for one address space.
struct ProvenanceInfo
{
    std::vector<ProvenanceSpan> spans;
};

// A normalized root write before flattening into buffer provenance state.
// buffer[offset, offset + size) currently comes from one root payload slice.
struct RootWriteToBufferRange
{
    format::HandleId   buffer = 0;
    VkDeviceSize       offset = 0;
    uint64_t           size   = 0;
    ProvenanceRootType type   = ProvenanceRootType::FillMemory;
    ProvenanceRootKey  root   = {};
};

// One buffer-backed use-site candidate location, expressed as a buffer-local
// byte offset relative to byte 0 of that buffer.
struct BufferUseSiteKey
{
    format::HandleId buffer           = format::kNullHandleId;
    VkDeviceSize     offset_in_buffer = 0;
};

// One dispatch read use-site candidate location inside the current
// push-constant byte image of one command buffer.
struct DispatchPushConstantUseSiteKey
{
    format::HandleId command_buffer           = format::kNullHandleId;
    VkDeviceSize     offset_in_push_constants = 0;
};

// Source-kind-aware dispatch use-site key. Buffer-backed sources reuse
// BufferUseSiteKey; push constants use their own command-buffer-local arm.
using DispatchUseSiteKey = std::variant<BufferUseSiteKey, DispatchPushConstantUseSiteKey>;

// Verified use-site consumed by one BuildAS action at execute time.
struct VerifiedBuildAsUseSite
{
    uint64_t         build_call_index           = 0;
    uint32_t         info_index                 = 0;
    format::HandleId dst_as                     = 0;
    BufferUseSiteKey use_site                   = {};
    VkDeviceAddress  value                      = 0;
    format::HandleId referenced_as              = format::kNullHandleId;
    VkDeviceAddress  referenced_as_base_address = 0;
    VkDeviceSize     referenced_as_size         = 0;
};

// Verified dispatch use-site consumed by one simulator-reported buffer device
// address read at execute time.
struct VerifiedDispatchBufferAddressUseSite
{
    DispatchUseSiteKey use_site                       = {};
    VkDeviceAddress    value                          = 0;
    format::HandleId   referenced_buffer              = format::kNullHandleId;
    VkDeviceAddress    referenced_buffer_base_address = 0;
    VkDeviceSize       referenced_buffer_size         = 0;
};

enum class RejectedDeviceAddressUseSiteReason
{
    ZeroValue,
    UnknownDeviceAddress,
    NotLiveAtUseTime,
};

// Candidate use-site whose slot position is correct, but whose current
// execute-time value did not pass live device-address verification.
struct RejectedBuildAsUseSite
{
    uint64_t                           build_call_index = 0;
    uint32_t                           info_index       = 0;
    format::HandleId                   dst_as           = 0;
    BufferUseSiteKey                   use_site         = {};
    VkDeviceAddress                    value            = 0;
    RejectedDeviceAddressUseSiteReason reason           = RejectedDeviceAddressUseSiteReason::UnknownDeviceAddress;
};

// Candidate dispatch use-site whose slot position was normalized successfully,
// but whose current execute-time value did not pass live device-address verification.
struct RejectedDispatchBufferAddressUseSite
{
    DispatchUseSiteKey                 use_site = {};
    VkDeviceAddress                    value    = 0;
    RejectedDeviceAddressUseSiteReason reason   = RejectedDeviceAddressUseSiteReason::UnknownDeviceAddress;
};

// One resolved root range for the queried use-site byte range.
struct ResolvedRootRange
{
    ProvenanceRootType type = ProvenanceRootType::FillMemory;
    ProvenanceRootKey  root = {};
    uint64_t           size = 0; // query_size
};

// Verified BuildAS use-site that was successfully resolved to one root.
struct ResolvedBuildAsUseSite
{
    VerifiedBuildAsUseSite verified_use_site = {};
    ResolvedRootRange      resolved_root     = {};
};

// Verified dispatch use-site that was successfully resolved to one root.
struct ResolvedDispatchBufferAddressUseSite
{
    VerifiedDispatchBufferAddressUseSite verified_use_site = {};
    ResolvedRootRange                    resolved_root     = {};
};

enum class UnresolvedDeviceAddressUseSiteReason
{
    MissingBufferProvenance,
    MissingPushConstantProvenance,
    UncoveredRange,
    CrossSpanBoundary,
    UnsupportedRootType,
};

// Verified BuildAS use-site whose value was valid, but whose provenance did
// not resolve to one root range at the BuildAS execute point.
struct UnresolvedBuildAsUseSite
{
    VerifiedBuildAsUseSite               verified_use_site = {};
    UnresolvedDeviceAddressUseSiteReason reason = UnresolvedDeviceAddressUseSiteReason::MissingBufferProvenance;
};

// Verified dispatch use-site whose value was valid, but whose provenance did
// not resolve to one root range at the dispatch execute point.
struct UnresolvedDispatchBufferAddressUseSite
{
    VerifiedDispatchBufferAddressUseSite verified_use_site = {};
    UnresolvedDeviceAddressUseSiteReason reason = UnresolvedDeviceAddressUseSiteReason::MissingBufferProvenance;
};

// Dispatch-only normalize failures before a simulator-reported use-site can be
// converted into a stable use-site key.
enum class UnsupportedDispatchBufferAddressUseSiteReason
{
    UnsupportedBitComponentCount,
    UnsupportedBitLocation,
    UnsupportedBitAlignment,
    UnsupportedBitWidth,
    UnsupportedStorageClass,
    MissingBlockInfo,
    BlockRangeOutOfBounds,
    MissingPushConstantBlockInfo,
    PushConstantRangeOutOfBounds,
};

// Shared pure-data bridge from modifier-owned analysis results to fixup location construction.
struct DeviceAddressFixupInput
{
    format::HandleId  referenced_object      = format::kNullHandleId;
    uint64_t          referenced_object_size = 0;
    VkDeviceAddress   original_address       = 0;
    VkDeviceAddress   adjusted_address       = 0;
    ResolvedRootRange resolved_root          = {};
};

// Root-source-local fixup locations finalized after analysis and consumed
// by the modification pass. Different fixup payload kinds share the same
// root grouping.
struct FixupLocations
{
    std::vector<format::AddressLocationInfo>        address_locations;
    std::vector<format::DescriptorDataLocationInfo> descriptor_data_locations;
};
// call index/ fill_serial_index -> FixupLocations
using FixupLocationsBySourceIndex = std::unordered_map<uint64_t, FixupLocations>;

GFXRECON_END_NAMESPACE(decode)
GFXRECON_END_NAMESPACE(gfxrecon)

#endif // GFXRECON_TOOLS_OPTIMIZE_VULKAN_SPIRV_TRACKER_TYPES_H