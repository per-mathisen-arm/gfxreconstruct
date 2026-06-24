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
#ifndef GFXRECON_TOOLS_OPTIMIZE_VULKAN_SPIRV_TRACKER_PROVENANCE_TRACKER_H
#define GFXRECON_TOOLS_OPTIMIZE_VULKAN_SPIRV_TRACKER_PROVENANCE_TRACKER_H

#include <unordered_map>
#include <vector>

#include "util/defines.h"
#include "vulkan_spirv_tracker_types.h"

GFXRECON_BEGIN_NAMESPACE(gfxrecon)
GFXRECON_BEGIN_NAMESPACE(decode)

// Pure-data provenance helpers shared by tracker-owned state mutation and resolve wrappers.
// They stay outside ProvenanceTracker because they do not depend on tracker-owned state.

void ClearProvenanceInfoRange(ProvenanceInfo& info, uint64_t offset, uint64_t size);
bool CanMergeProvenanceSpans(const ProvenanceSpan& lhs, const ProvenanceSpan& rhs);
void AppendProvenanceSpan(std::vector<ProvenanceSpan>& spans, const ProvenanceSpan& span);
void FlattenProvenanceInfoForSpan(ProvenanceInfo& info, const ProvenanceSpan& write_span);
bool ResolveProvenanceRangeToRoot(const ProvenanceInfo&                 provenance,
                                  uint64_t                              offset,
                                  uint64_t                              query_size,
                                  ResolvedRootRange&                    out,
                                  UnresolvedDeviceAddressUseSiteReason& reason);

// Owns the mutable flattened provenance state used during analysis-time replay.
// Buffer contents and push-constant contents live in different Vulkan-visible
// address spaces, but both are tracked with the same ProvenanceInfo substrate.
class ProvenanceTracker
{
  public:
    void Reset();
    void ClearBufferProvenanceRange(format::HandleId buffer, uint64_t offset, uint64_t size);
    void ClearPushConstantProvenance(format::HandleId command_buffer);
    void ErasePushConstantProvenance(format::HandleId command_buffer);

    void FlattenBufferProvenanceForRootWrite(const RootWriteToBufferRange& write);
    void FlattenPushConstantProvenanceForWrite(format::HandleId         command_buffer,
                                               uint32_t                 push_constant_offset,
                                               uint32_t                 size,
                                               const ProvenanceRootKey& root);

    std::vector<RootWriteToBufferRange> BuildRootWritesFromBufferRange(format::HandleId src_buffer,
                                                                       VkDeviceSize     src_offset,
                                                                       format::HandleId dst_buffer,
                                                                       VkDeviceSize     dst_offset,
                                                                       VkDeviceSize     size) const;

    std::vector<RootWriteToBufferRange> BuildRootWritesFromCopyRegions(format::HandleId                 src_buffer,
                                                                       format::HandleId                 dst_buffer,
                                                                       const std::vector<VkBufferCopy>& regions);

    bool ResolveBufferRangeToRoot(format::HandleId                      buffer,
                                  uint64_t                              offset,
                                  uint64_t                              query_size,
                                  ResolvedRootRange&                    out,
                                  UnresolvedDeviceAddressUseSiteReason& reason) const;

    bool ResolvePushConstantRangeToRoot(format::HandleId                      command_buffer,
                                        uint64_t                              offset_in_push_constants,
                                        uint64_t                              query_size,
                                        ResolvedRootRange&                    out,
                                        UnresolvedDeviceAddressUseSiteReason& reason) const;

  private:
    // buffer id -> ProvenanceInfo
    std::unordered_map<format::HandleId, ProvenanceInfo> buffer_provenance_entries_;
    // commandBuffer id -> ProvenanceInfo
    std::unordered_map<format::HandleId, ProvenanceInfo> push_constant_provenance_entries_;
};

GFXRECON_END_NAMESPACE(decode)
GFXRECON_END_NAMESPACE(gfxrecon)

#endif // GFXRECON_TOOLS_OPTIMIZE_VULKAN_SPIRV_TRACKER_PROVENANCE_TRACKER_H