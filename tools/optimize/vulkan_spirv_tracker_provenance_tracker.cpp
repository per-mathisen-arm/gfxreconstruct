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
#include <algorithm>

#include "vulkan_spirv_tracker_provenance_tracker.h"

GFXRECON_BEGIN_NAMESPACE(gfxrecon)
GFXRECON_BEGIN_NAMESPACE(decode)

bool ResolveProvenanceRangeToRoot(const ProvenanceInfo&                 provenance,
                                  uint64_t                              offset,
                                  uint64_t                              query_size,
                                  ResolvedRootRange&                    out,
                                  UnresolvedDeviceAddressUseSiteReason& reason)
{
    GFXRECON_ASSERT(query_size > 0);

    const uint64_t query_begin = offset;
    const uint64_t query_end   = offset + query_size;
    const auto&    spans       = provenance.spans;

    for (const ProvenanceSpan& span : spans)
    {
        const uint64_t span_begin = span.offset;
        const uint64_t span_end   = span.offset + span.size;

        if (span_end <= query_begin)
        {
            continue;
        }

        if (span_begin > query_begin)
        {
            reason = UnresolvedDeviceAddressUseSiteReason::UncoveredRange;
            return false;
        }

        if (query_end > span_end)
        {
            reason = UnresolvedDeviceAddressUseSiteReason::CrossSpanBoundary;
            return false;
        }

        switch (span.type)
        {
            case ProvenanceRootType::FillMemory:
            case ProvenanceRootType::UpdateBuffer:
            case ProvenanceRootType::PushConstants:
                out.type                = span.type;
                out.root.source_index   = span.root.source_index;
                out.root.payload_offset = span.root.payload_offset + (query_begin - span_begin);
                out.size                = query_size;
                return true;
            default:
                reason = UnresolvedDeviceAddressUseSiteReason::UnsupportedRootType;
                return false;
        }
    }

    reason = UnresolvedDeviceAddressUseSiteReason::UncoveredRange;
    return false;
}

void FlattenProvenanceInfoForSpan(ProvenanceInfo& info, const ProvenanceSpan& write_span)
{
    if (write_span.size == 0)
    {
        return;
    }

    const auto&                 old_spans = info.spans;
    std::vector<ProvenanceSpan> new_spans;
    new_spans.reserve(old_spans.size() + 2);
    const uint64_t write_begin = write_span.offset;
    const uint64_t write_end   = write_begin + write_span.size;
    bool           inserted    = false;

    auto append_new_span = [&]() {
        if (inserted)
        {
            return;
        }

        AppendProvenanceSpan(new_spans, write_span);
        inserted = true;
    };

    for (const auto& span : old_spans)
    {
        const uint64_t old_begin = span.offset;
        const uint64_t old_end   = old_begin + span.size;

        if (old_end <= write_begin)
        {
            AppendProvenanceSpan(new_spans, span);
            continue;
        }

        if (old_begin >= write_end)
        {
            append_new_span();
            AppendProvenanceSpan(new_spans, span);
            continue;
        }

        if (old_begin < write_begin)
        {
            AppendProvenanceSpan(new_spans, ProvenanceSpan{ old_begin, write_begin - old_begin, span.type, span.root });
        }

        append_new_span();

        if (write_end < old_end)
        {
            ProvenanceRootKey right_root = span.root;
            right_root.payload_offset += (write_end - old_begin);
            AppendProvenanceSpan(new_spans, ProvenanceSpan{ write_end, old_end - write_end, span.type, right_root });
        }
    }

    append_new_span();
    info.spans = std::move(new_spans);
}

bool CanMergeProvenanceSpans(const ProvenanceSpan& lhs, const ProvenanceSpan& rhs)
{
    if ((lhs.size == 0) || (rhs.size == 0))
    {
        return false;
    }

    if ((lhs.offset + lhs.size) != rhs.offset)
    {
        return false;
    }

    if ((lhs.type != rhs.type) || (lhs.root.source_index != rhs.root.source_index))
    {
        return false;
    }

    return (lhs.root.payload_offset + lhs.size) == rhs.root.payload_offset;
}

void AppendProvenanceSpan(std::vector<ProvenanceSpan>& spans, const ProvenanceSpan& span)
{
    if (span.size == 0)
    {
        return;
    }

    if (!spans.empty() && CanMergeProvenanceSpans(spans.back(), span))
    {
        spans.back().size += span.size;
        return;
    }

    spans.push_back(span);
}

void ClearProvenanceInfoRange(ProvenanceInfo& info, uint64_t offset, uint64_t size)
{
    if (size == 0)
    {
        return;
    }

    const auto&                 old_spans = info.spans;
    std::vector<ProvenanceSpan> new_spans;
    new_spans.reserve(old_spans.size());

    const uint64_t clear_begin = offset;
    const uint64_t clear_end   = clear_begin + size;

    for (const auto& span : old_spans)
    {
        const uint64_t old_begin = span.offset;
        const uint64_t old_end   = old_begin + span.size;

        if (old_end <= clear_begin || old_begin >= clear_end)
        {
            AppendProvenanceSpan(new_spans, span);
            continue;
        }

        if (old_begin < clear_begin)
        {
            AppendProvenanceSpan(new_spans, ProvenanceSpan{ old_begin, clear_begin - old_begin, span.type, span.root });
        }

        if (clear_end < old_end)
        {
            ProvenanceRootKey right_root = span.root;
            right_root.payload_offset += (clear_end - old_begin);
            AppendProvenanceSpan(new_spans, ProvenanceSpan{ clear_end, old_end - clear_end, span.type, right_root });
        }
    }

    info.spans = std::move(new_spans);
}

void ProvenanceTracker::Reset()
{
    buffer_provenance_entries_.clear();
    push_constant_provenance_entries_.clear();
}

void ProvenanceTracker::ClearBufferProvenanceRange(format::HandleId buffer, uint64_t offset, uint64_t size)
{
    const auto info_iter = buffer_provenance_entries_.find(buffer);
    if (info_iter == buffer_provenance_entries_.end())
    {
        return;
    }

    ClearProvenanceInfoRange(info_iter->second, offset, size);
}

void ProvenanceTracker::ClearPushConstantProvenance(format::HandleId command_buffer)
{
    push_constant_provenance_entries_[command_buffer].spans.clear();
}

void ProvenanceTracker::ErasePushConstantProvenance(format::HandleId command_buffer)
{
    push_constant_provenance_entries_.erase(command_buffer);
}

void ProvenanceTracker::FlattenBufferProvenanceForRootWrite(const RootWriteToBufferRange& write)
{
    FlattenProvenanceInfoForSpan(buffer_provenance_entries_[write.buffer],
                                 ProvenanceSpan{ write.offset, write.size, write.type, write.root });
}

void ProvenanceTracker::FlattenPushConstantProvenanceForWrite(format::HandleId         command_buffer,
                                                              uint32_t                 push_constant_offset,
                                                              uint32_t                 size,
                                                              const ProvenanceRootKey& root)
{
    FlattenProvenanceInfoForSpan(push_constant_provenance_entries_[command_buffer],
                                 ProvenanceSpan{ push_constant_offset, size, ProvenanceRootType::PushConstants, root });
}

std::vector<RootWriteToBufferRange> ProvenanceTracker::BuildRootWritesFromBufferRange(format::HandleId src_buffer,
                                                                                      VkDeviceSize     src_offset,
                                                                                      format::HandleId dst_buffer,
                                                                                      VkDeviceSize     dst_offset,
                                                                                      VkDeviceSize     size) const
{
    std::vector<RootWriteToBufferRange> writes;
    if (size == 0)
    {
        return writes;
    }

    const auto src_info_iter = buffer_provenance_entries_.find(src_buffer);
    if (src_info_iter == buffer_provenance_entries_.end())
    {
        return writes;
    }

    const uint64_t src_begin = src_offset;
    const uint64_t src_end   = src_begin + size;

    for (const auto& span : src_info_iter->second.spans)
    {
        const uint64_t old_begin = span.offset;
        const uint64_t old_end   = old_begin + span.size;

        if (old_end <= src_begin)
        {
            continue;
        }
        if (old_begin >= src_end)
        {
            break;
        }

        const uint64_t overlap_begin = std::max(src_begin, old_begin);
        const uint64_t overlap_end   = std::min(src_end, old_end);
        if (overlap_begin >= overlap_end)
        {
            continue;
        }

        ProvenanceRootKey rebased_root = span.root;
        rebased_root.payload_offset += (overlap_begin - old_begin);

        writes.push_back(RootWriteToBufferRange{ dst_buffer,
                                                 dst_offset + (overlap_begin - src_begin),
                                                 overlap_end - overlap_begin,
                                                 span.type,
                                                 rebased_root });
    }
    return writes;
}

std::vector<RootWriteToBufferRange> ProvenanceTracker::BuildRootWritesFromCopyRegions(
    format::HandleId src_buffer, format::HandleId dst_buffer, const std::vector<VkBufferCopy>& regions)
{
    std::vector<RootWriteToBufferRange> writes;

    for (const auto& region : regions)
    {
        if (region.size == 0)
        {
            continue;
        }

        auto& dst_info = buffer_provenance_entries_[dst_buffer];
        ClearProvenanceInfoRange(dst_info, region.dstOffset, region.size);

        auto region_writes =
            BuildRootWritesFromBufferRange(src_buffer, region.srcOffset, dst_buffer, region.dstOffset, region.size);
        writes.insert(writes.end(), region_writes.begin(), region_writes.end());
    }

    return writes;
}

bool ProvenanceTracker::ResolveBufferRangeToRoot(format::HandleId                      buffer,
                                                 uint64_t                              offset,
                                                 uint64_t                              query_size,
                                                 ResolvedRootRange&                    out,
                                                 UnresolvedDeviceAddressUseSiteReason& reason) const
{
    GFXRECON_ASSERT(query_size > 0);

    const auto info_iter = buffer_provenance_entries_.find(buffer);
    if (info_iter == buffer_provenance_entries_.end())
    {
        reason = UnresolvedDeviceAddressUseSiteReason::MissingBufferProvenance;
        return false;
    }

    reason = UnresolvedDeviceAddressUseSiteReason::UncoveredRange;
    return ResolveProvenanceRangeToRoot(info_iter->second, offset, query_size, out, reason);
}

bool ProvenanceTracker::ResolvePushConstantRangeToRoot(format::HandleId                      command_buffer,
                                                       uint64_t                              offset_in_push_constants,
                                                       uint64_t                              query_size,
                                                       ResolvedRootRange&                    out,
                                                       UnresolvedDeviceAddressUseSiteReason& reason) const
{
    GFXRECON_ASSERT(query_size > 0);

    const auto info_iter = push_constant_provenance_entries_.find(command_buffer);
    if (info_iter == push_constant_provenance_entries_.end() || info_iter->second.spans.empty())
    {
        reason = UnresolvedDeviceAddressUseSiteReason::MissingPushConstantProvenance;
        return false;
    }

    reason = UnresolvedDeviceAddressUseSiteReason::UncoveredRange;
    return ResolveProvenanceRangeToRoot(info_iter->second, offset_in_push_constants, query_size, out, reason);
}

GFXRECON_END_NAMESPACE(decode)
GFXRECON_END_NAMESPACE(gfxrecon)