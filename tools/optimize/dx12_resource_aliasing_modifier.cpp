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

#include "dx12_resource_aliasing_modifier.h"
#include "format/format.h"
#include "format/format_arm.h"

#include <algorithm>

GFXRECON_BEGIN_NAMESPACE(gfxrecon)
GFXRECON_BEGIN_NAMESPACE(decode)

static_assert(sizeof(D3D12_RESOURCE_DESC1) <= sizeof(format::arm::Dx12ResourceAliasingInfo::resource_desc),
              "D3D12_RESOURCE_DESC1 does not fit in Dx12ResourceAliasingInfo::resource_desc[12]");

// ---------------------------------------------------------------------------
// Key helpers
// ---------------------------------------------------------------------------

std::vector<uint8_t> Dx12ResourceAliasingModifier::MakeDescKey(const D3D12_RESOURCE_DESC1& src)
{
    // Zero-init to canonicalize compiler padding (e.g. 4 bytes after Dimension).
    D3D12_RESOURCE_DESC1 key{};
    key.Dimension                = src.Dimension;
    key.Alignment                = src.Alignment;
    key.Width                    = src.Width;
    key.Height                   = src.Height;
    key.DepthOrArraySize         = src.DepthOrArraySize;
    key.MipLevels                = src.MipLevels;
    key.Format                   = src.Format;
    key.SampleDesc               = src.SampleDesc;
    key.Layout                   = src.Layout;
    key.Flags                    = src.Flags;
    key.SamplerFeedbackMipRegion = src.SamplerFeedbackMipRegion;
    return std::vector<uint8_t>(reinterpret_cast<const uint8_t*>(&key),
                                reinterpret_cast<const uint8_t*>(&key) + sizeof(key));
}

std::vector<uint8_t> Dx12ResourceAliasingModifier::MakeDescKey(const D3D12_RESOURCE_DESC& src)
{
    D3D12_RESOURCE_DESC1 key{};
    key.Dimension        = src.Dimension;
    key.Alignment        = src.Alignment;
    key.Width            = src.Width;
    key.Height           = src.Height;
    key.DepthOrArraySize = src.DepthOrArraySize;
    key.MipLevels        = src.MipLevels;
    key.Format           = src.Format;
    key.SampleDesc       = src.SampleDesc;
    key.Layout           = src.Layout;
    key.Flags            = src.Flags;
    // SamplerFeedbackMipRegion stays zero (no field in D3D12_RESOURCE_DESC).
    return std::vector<uint8_t>(reinterpret_cast<const uint8_t*>(&key),
                                reinterpret_cast<const uint8_t*>(&key) + sizeof(key));
}

// ---------------------------------------------------------------------------
// GetResourceAllocationInfo* interception
// ---------------------------------------------------------------------------

void Dx12ResourceAliasingModifier::Process_ID3D12Device_GetResourceAllocationInfo(
    const ApiCallInfo&                                 call_info,
    format::HandleId                                   object_id,
    Decoded_D3D12_RESOURCE_ALLOCATION_INFO             return_value,
    UINT                                               visibleMask,
    UINT                                               numResourceDescs,
    StructPointerDecoder<Decoded_D3D12_RESOURCE_DESC>* pResourceDescs)
{
    GFXRECON_UNREFERENCED_PARAMETER(call_info);
    GFXRECON_UNREFERENCED_PARAMETER(object_id);
    GFXRECON_UNREFERENCED_PARAMETER(visibleMask);

    if (IsModificationPass())
    {
        return;
    }

    if ((pResourceDescs == nullptr) || (pResourceDescs->GetPointer() == nullptr))
    {
        return;
    }

    if ((return_value.decoded_value == nullptr) || (return_value.decoded_value->SizeInBytes == 0) ||
        (return_value.decoded_value->SizeInBytes == UINT64_MAX))
    {
        return;
    }

    // Base variant: no per-resource output array.  Only reliable when querying a single desc.
    if (numResourceDescs != 1)
    {
        return;
    }

    const D3D12_RESOURCE_DESC& desc = *pResourceDescs->GetPointer();
    uint64_t                   size = return_value.decoded_value->SizeInBytes;
    desc_to_allocation_size_.emplace(MakeDescKey(desc), size);
}

void Dx12ResourceAliasingModifier::Process_ID3D12Device4_GetResourceAllocationInfo1(
    const ApiCallInfo&                                             call_info,
    format::HandleId                                               object_id,
    Decoded_D3D12_RESOURCE_ALLOCATION_INFO                         return_value,
    UINT                                                           visibleMask,
    UINT                                                           numResourceDescs,
    StructPointerDecoder<Decoded_D3D12_RESOURCE_DESC>*             pResourceDescs,
    StructPointerDecoder<Decoded_D3D12_RESOURCE_ALLOCATION_INFO1>* pResourceAllocationInfo1)
{
    GFXRECON_UNREFERENCED_PARAMETER(call_info);
    GFXRECON_UNREFERENCED_PARAMETER(object_id);
    GFXRECON_UNREFERENCED_PARAMETER(visibleMask);
    GFXRECON_UNREFERENCED_PARAMETER(return_value);

    if (IsModificationPass())
    {
        return;
    }

    if ((pResourceDescs == nullptr) || (pResourceDescs->GetPointer() == nullptr) || (numResourceDescs == 0))
    {
        return;
    }

    // Per-resource output array gives exact size for each desc.
    if ((pResourceAllocationInfo1 != nullptr) && (pResourceAllocationInfo1->GetPointer() != nullptr))
    {
        const D3D12_RESOURCE_DESC*             descs  = pResourceDescs->GetPointer();
        const D3D12_RESOURCE_ALLOCATION_INFO1* allocs = pResourceAllocationInfo1->GetPointer();
        for (UINT i = 0; i < numResourceDescs; ++i)
        {
            if ((allocs[i].SizeInBytes != 0) && (allocs[i].SizeInBytes != UINT64_MAX))
            {
                desc_to_allocation_size_.emplace(MakeDescKey(descs[i]), allocs[i].SizeInBytes);
            }
        }
        return;
    }

    // Fallback: no per-resource array, use total only when single desc.
    if (numResourceDescs != 1)
    {
        return;
    }
    if ((return_value.decoded_value != nullptr) && (return_value.decoded_value->SizeInBytes != 0) &&
        (return_value.decoded_value->SizeInBytes != UINT64_MAX))
    {
        desc_to_allocation_size_.emplace(MakeDescKey(*pResourceDescs->GetPointer()),
                                         return_value.decoded_value->SizeInBytes);
    }
}

void Dx12ResourceAliasingModifier::Process_ID3D12Device8_GetResourceAllocationInfo2(
    const ApiCallInfo&                                             call_info,
    format::HandleId                                               object_id,
    Decoded_D3D12_RESOURCE_ALLOCATION_INFO                         return_value,
    UINT                                                           visibleMask,
    UINT                                                           numResourceDescs,
    StructPointerDecoder<Decoded_D3D12_RESOURCE_DESC1>*            pResourceDescs,
    StructPointerDecoder<Decoded_D3D12_RESOURCE_ALLOCATION_INFO1>* pResourceAllocationInfo1)
{
    GFXRECON_UNREFERENCED_PARAMETER(call_info);
    GFXRECON_UNREFERENCED_PARAMETER(object_id);
    GFXRECON_UNREFERENCED_PARAMETER(visibleMask);
    GFXRECON_UNREFERENCED_PARAMETER(return_value);

    if (IsModificationPass())
    {
        return;
    }

    if ((pResourceDescs == nullptr) || (pResourceDescs->GetPointer() == nullptr) || (numResourceDescs == 0))
    {
        return;
    }

    if ((pResourceAllocationInfo1 != nullptr) && (pResourceAllocationInfo1->GetPointer() != nullptr))
    {
        const D3D12_RESOURCE_DESC1*            descs  = pResourceDescs->GetPointer();
        const D3D12_RESOURCE_ALLOCATION_INFO1* allocs = pResourceAllocationInfo1->GetPointer();
        for (UINT i = 0; i < numResourceDescs; ++i)
        {
            if ((allocs[i].SizeInBytes != 0) && (allocs[i].SizeInBytes != UINT64_MAX))
            {
                desc_to_allocation_size_.emplace(MakeDescKey(descs[i]), allocs[i].SizeInBytes);
            }
        }
        return;
    }

    if (numResourceDescs != 1)
    {
        return;
    }
    if ((return_value.decoded_value != nullptr) && (return_value.decoded_value->SizeInBytes != 0) &&
        (return_value.decoded_value->SizeInBytes != UINT64_MAX))
    {
        desc_to_allocation_size_.emplace(MakeDescKey(*pResourceDescs->GetPointer()),
                                         return_value.decoded_value->SizeInBytes);
    }
}

void Dx12ResourceAliasingModifier::Process_ID3D12Device12_GetResourceAllocationInfo3(
    const ApiCallInfo&                                             call_info,
    format::HandleId                                               object_id,
    Decoded_D3D12_RESOURCE_ALLOCATION_INFO                         return_value,
    UINT                                                           visibleMask,
    UINT                                                           numResourceDescs,
    StructPointerDecoder<Decoded_D3D12_RESOURCE_DESC1>*            pResourceDescs,
    PointerDecoder<UINT32>*                                        pNumCastableFormats,
    PointerDecoder<DXGI_FORMAT*>*                                  ppCastableFormats,
    StructPointerDecoder<Decoded_D3D12_RESOURCE_ALLOCATION_INFO1>* pResourceAllocationInfo1)
{
    GFXRECON_UNREFERENCED_PARAMETER(call_info);
    GFXRECON_UNREFERENCED_PARAMETER(object_id);
    GFXRECON_UNREFERENCED_PARAMETER(visibleMask);
    GFXRECON_UNREFERENCED_PARAMETER(return_value);
    GFXRECON_UNREFERENCED_PARAMETER(pNumCastableFormats);
    GFXRECON_UNREFERENCED_PARAMETER(ppCastableFormats);

    if (IsModificationPass())
    {
        return;
    }

    if ((pResourceDescs == nullptr) || (pResourceDescs->GetPointer() == nullptr) || (numResourceDescs == 0))
    {
        return;
    }

    if ((pResourceAllocationInfo1 != nullptr) && (pResourceAllocationInfo1->GetPointer() != nullptr))
    {
        const D3D12_RESOURCE_DESC1*            descs  = pResourceDescs->GetPointer();
        const D3D12_RESOURCE_ALLOCATION_INFO1* allocs = pResourceAllocationInfo1->GetPointer();
        for (UINT i = 0; i < numResourceDescs; ++i)
        {
            if ((allocs[i].SizeInBytes != 0) && (allocs[i].SizeInBytes != UINT64_MAX))
            {
                desc_to_allocation_size_.emplace(MakeDescKey(descs[i]), allocs[i].SizeInBytes);
            }
        }
        return;
    }

    if (numResourceDescs != 1)
    {
        return;
    }
    if ((return_value.decoded_value != nullptr) && (return_value.decoded_value->SizeInBytes != 0) &&
        (return_value.decoded_value->SizeInBytes != UINT64_MAX))
    {
        desc_to_allocation_size_.emplace(MakeDescKey(*pResourceDescs->GetPointer()),
                                         return_value.decoded_value->SizeInBytes);
    }
}

// ---------------------------------------------------------------------------
// Placed resource tracking
// ---------------------------------------------------------------------------

void Dx12ResourceAliasingModifier::TrackCreatePlacedResource(const ApiCallInfo&           call_info,
                                                             HRESULT                      return_value,
                                                             format::HandleId             pHeap,
                                                             UINT64                       HeapOffset,
                                                             const D3D12_RESOURCE_DESC1&  resource_desc,
                                                             HandlePointerDecoder<void*>* ppvResource)
{
    if (return_value != S_OK)
    {
        return;
    }

    if ((ppvResource == nullptr) || (ppvResource->GetPointer() == nullptr))
    {
        return;
    }

    format::HandleId resource_id = *ppvResource->GetPointer();
    if (resource_id == format::kNullHandleId)
    {
        return;
    }

    PlacedResourceInfo info{};
    info.resource_id       = resource_id;
    info.heap_id           = pHeap;
    info.heap_offset       = HeapOffset;
    info.resource_desc     = resource_desc;
    info.creation_index    = call_info.index;
    info.destruction_index = UINT64_MAX;

    // Look up allocation size recorded from a prior GetResourceAllocationInfo* call.
    auto it = desc_to_allocation_size_.find(MakeDescKey(resource_desc));
    if (it != desc_to_allocation_size_.end())
    {
        info.estimated_size = it->second;
    }

    placed_resource_infos_[resource_id] = info;
}

void Dx12ResourceAliasingModifier::Process_ID3D12Device_CreatePlacedResource(
    const ApiCallInfo&                                 call_info,
    format::HandleId                                   object_id,
    HRESULT                                            return_value,
    format::HandleId                                   pHeap,
    UINT64                                             HeapOffset,
    StructPointerDecoder<Decoded_D3D12_RESOURCE_DESC>* pDesc,
    D3D12_RESOURCE_STATES                              InitialState,
    StructPointerDecoder<Decoded_D3D12_CLEAR_VALUE>*   pOptimizedClearValue,
    Decoded_GUID                                       riid,
    HandlePointerDecoder<void*>*                       ppvResource)
{
    GFXRECON_UNREFERENCED_PARAMETER(object_id);
    GFXRECON_UNREFERENCED_PARAMETER(InitialState);
    GFXRECON_UNREFERENCED_PARAMETER(pOptimizedClearValue);
    GFXRECON_UNREFERENCED_PARAMETER(riid);

    if (IsModificationPass())
    {
        if ((return_value == S_OK) && (ppvResource != nullptr) && (ppvResource->GetPointer() != nullptr))
        {
            const format::HandleId resource_id = *ppvResource->GetPointer();
            if (aliasing_resource_ids_.find(resource_id) != aliasing_resource_ids_.end())
            {
                InjectAliasingMetaData(resource_id);
            }
        }
        return;
    }

    // Promote D3D12_RESOURCE_DESC -> D3D12_RESOURCE_DESC1 (SamplerFeedbackMipRegion zero-init).
    D3D12_RESOURCE_DESC1 desc1{};
    if ((pDesc != nullptr) && (pDesc->GetPointer() != nullptr))
    {
        const D3D12_RESOURCE_DESC& src = *pDesc->GetPointer();
        std::memcpy(&desc1, &src, sizeof(D3D12_RESOURCE_DESC));
    }
    TrackCreatePlacedResource(call_info, return_value, pHeap, HeapOffset, desc1, ppvResource);
}

void Dx12ResourceAliasingModifier::Process_ID3D12Device8_CreatePlacedResource1(
    const ApiCallInfo&                                  call_info,
    format::HandleId                                    object_id,
    HRESULT                                             return_value,
    format::HandleId                                    pHeap,
    UINT64                                              HeapOffset,
    StructPointerDecoder<Decoded_D3D12_RESOURCE_DESC1>* pDesc,
    D3D12_RESOURCE_STATES                               InitialState,
    StructPointerDecoder<Decoded_D3D12_CLEAR_VALUE>*    pOptimizedClearValue,
    Decoded_GUID                                        riid,
    HandlePointerDecoder<void*>*                        ppvResource)
{
    GFXRECON_UNREFERENCED_PARAMETER(object_id);
    GFXRECON_UNREFERENCED_PARAMETER(InitialState);
    GFXRECON_UNREFERENCED_PARAMETER(pOptimizedClearValue);
    GFXRECON_UNREFERENCED_PARAMETER(riid);

    if (IsModificationPass())
    {
        if ((return_value == S_OK) && (ppvResource != nullptr) && (ppvResource->GetPointer() != nullptr))
        {
            const format::HandleId resource_id = *ppvResource->GetPointer();
            if (aliasing_resource_ids_.find(resource_id) != aliasing_resource_ids_.end())
            {
                InjectAliasingMetaData(resource_id);
            }
        }
        return;
    }

    const D3D12_RESOURCE_DESC1 desc1 =
        ((pDesc != nullptr) && (pDesc->GetPointer() != nullptr)) ? *pDesc->GetPointer() : D3D12_RESOURCE_DESC1{};
    TrackCreatePlacedResource(call_info, return_value, pHeap, HeapOffset, desc1, ppvResource);
}

void Dx12ResourceAliasingModifier::Process_ID3D12Device10_CreatePlacedResource2(
    const ApiCallInfo&                                  call_info,
    format::HandleId                                    object_id,
    HRESULT                                             return_value,
    format::HandleId                                    pHeap,
    UINT64                                              HeapOffset,
    StructPointerDecoder<Decoded_D3D12_RESOURCE_DESC1>* pDesc,
    D3D12_BARRIER_LAYOUT                                InitialLayout,
    StructPointerDecoder<Decoded_D3D12_CLEAR_VALUE>*    pOptimizedClearValue,
    UINT32                                              NumCastableFormats,
    PointerDecoder<DXGI_FORMAT>*                        pCastableFormats,
    Decoded_GUID                                        riid,
    HandlePointerDecoder<void*>*                        ppvResource)
{
    GFXRECON_UNREFERENCED_PARAMETER(object_id);
    GFXRECON_UNREFERENCED_PARAMETER(InitialLayout);
    GFXRECON_UNREFERENCED_PARAMETER(pOptimizedClearValue);
    GFXRECON_UNREFERENCED_PARAMETER(NumCastableFormats);
    GFXRECON_UNREFERENCED_PARAMETER(pCastableFormats);
    GFXRECON_UNREFERENCED_PARAMETER(riid);

    if (IsModificationPass())
    {
        if ((return_value == S_OK) && (ppvResource != nullptr) && (ppvResource->GetPointer() != nullptr))
        {
            const format::HandleId resource_id = *ppvResource->GetPointer();
            if (aliasing_resource_ids_.find(resource_id) != aliasing_resource_ids_.end())
            {
                InjectAliasingMetaData(resource_id);
            }
        }
        return;
    }

    const D3D12_RESOURCE_DESC1 desc1 =
        ((pDesc != nullptr) && (pDesc->GetPointer() != nullptr)) ? *pDesc->GetPointer() : D3D12_RESOURCE_DESC1{};
    TrackCreatePlacedResource(call_info, return_value, pHeap, HeapOffset, desc1, ppvResource);
}

void Dx12ResourceAliasingModifier::Process_ID3D12GraphicsCommandList_ResourceBarrier(
    const ApiCallInfo&                                    call_info,
    format::HandleId                                      object_id,
    UINT                                                  NumBarriers,
    StructPointerDecoder<Decoded_D3D12_RESOURCE_BARRIER>* pBarriers)
{
    GFXRECON_UNREFERENCED_PARAMETER(call_info);
    GFXRECON_UNREFERENCED_PARAMETER(object_id);

    if (IsModificationPass())
    {
        return;
    }

    if ((pBarriers == nullptr) || (pBarriers->GetPointer() == nullptr))
    {
        return;
    }

    auto* barriers = pBarriers->GetMetaStructPointer();
    for (UINT i = 0; i < NumBarriers; ++i)
    {
        const auto& barrier = barriers[i];
        if ((barrier.decoded_value == nullptr) ||
            (barrier.decoded_value->Type != D3D12_RESOURCE_BARRIER_TYPE_ALIASING) || (barrier.Aliasing == nullptr))
        {
            continue;
        }

        const format::HandleId before_id = barrier.Aliasing->pResourceBefore;
        const format::HandleId after_id  = barrier.Aliasing->pResourceAfter;

        if ((before_id != format::kNullHandleId) && (placed_resource_infos_.count(before_id) > 0))
        {
            aliasing_resource_ids_.insert(before_id);
        }
        if ((after_id != format::kNullHandleId) && (placed_resource_infos_.count(after_id) > 0))
        {
            aliasing_resource_ids_.insert(after_id);
        }
    }
}

void Dx12ResourceAliasingModifier::Process_IUnknown_Release(const ApiCallInfo& call_info,
                                                            format::HandleId   object_id,
                                                            ULONG              return_value)
{
    if (IsModificationPass())
    {
        return;
    }

    // Only record the destruction when the reference count reaches zero (final release).
    if (return_value != 0)
    {
        return;
    }

    auto it = placed_resource_infos_.find(object_id);
    if (it != placed_resource_infos_.end())
    {
        it->second.destruction_index = call_info.index;
    }
}

void Dx12ResourceAliasingModifier::ProcessDx12ResourceAliasingCommand(
    const format::arm::Dx12ResourceAliasingCommandHeader& command_header, const uint8_t* data)
{
    if (IsModificationPass())
    {
        // All old Dx12ResourceAliasingCommand Will be deleted
        SetDeleteCurrentCall();
        return;
    }
}

// ---------------------------------------------------------------------------
// Aliasing detection
// ---------------------------------------------------------------------------

void Dx12ResourceAliasingModifier::DetectImplicitAliasingResources()
{
    // Build a flat list sorted by (heap_id, heap_offset).
    std::vector<const PlacedResourceInfo*> sorted;
    sorted.reserve(placed_resource_infos_.size());
    std::transform(placed_resource_infos_.begin(),
                   placed_resource_infos_.end(),
                   std::back_inserter(sorted),
                   [](const auto& kv) { return &kv.second; });
    std::sort(sorted.begin(), sorted.end(), [](const PlacedResourceInfo* a, const PlacedResourceInfo* b) {
        return a->heap_id != b->heap_id ? a->heap_id < b->heap_id : a->heap_offset < b->heap_offset;
    });

    // If either resource is a texture and heap offsets differ, skip — different-offset
    // texture aliasing requires same-offset or explicit barrier to be well-defined.
    auto can_alias_pair = [](const PlacedResourceInfo* a, const PlacedResourceInfo* b) -> bool {
        if (a->heap_offset != b->heap_offset)
        {
            const bool a_is_tex = (a->resource_desc.Dimension != D3D12_RESOURCE_DIMENSION_BUFFER);
            const bool b_is_tex = (b->resource_desc.Dimension != D3D12_RESOURCE_DIMENSION_BUFFER);
            if (a_is_tex || b_is_tex)
            {
                return false;
            }
        }
        return true;
    };

    // Two resources alias if: same heap, heap byte ranges overlap, lifetimes overlap.
    // estimated_size == 0 (size unknown): fall back to exact same-offset comparison.
    for (size_t i = 0, n = sorted.size(); i < n; ++i)
    {
        const PlacedResourceInfo* a = sorted[i];
        const uint64_t a_end = (a->estimated_size > 0) ? (a->heap_offset + a->estimated_size) : (a->heap_offset + 1);

        for (size_t j = i + 1; j < n; ++j)
        {
            const PlacedResourceInfo* b = sorted[j];
            if (b->heap_id != a->heap_id)
            {
                break;
            }
            if (b->heap_offset >= a_end)
            {
                break; // sorted by offset; no further overlap with a
            }
            if ((a->creation_index < b->destruction_index) && (b->creation_index < a->destruction_index))
            {
                if (can_alias_pair(a, b))
                {
                    aliasing_resource_ids_.insert(a->resource_id);
                    aliasing_resource_ids_.insert(b->resource_id);
                }
            }
        }
    }
}

void Dx12ResourceAliasingModifier::BuildAliasingComponents()
{
    // Iterative union-find with path compression over aliasing_resource_infos_.
    std::unordered_map<format::HandleId, format::HandleId> parent;
    parent.reserve(aliasing_resource_infos_.size());
    for (const auto& [id, _] : aliasing_resource_infos_)
    {
        parent[id] = id;
    }

    auto find = [&parent](format::HandleId x) -> format::HandleId {
        // Find root.
        format::HandleId root = x;
        while (parent.at(root) != root)
        {
            root = parent.at(root);
        }
        // Path compression.
        while (parent.at(x) != root)
        {
            format::HandleId next = parent.at(x);
            parent[x]             = root;
            x                     = next;
        }
        return root;
    };
    auto unite = [&](format::HandleId a, format::HandleId b) {
        a = find(a);
        b = find(b);
        if (a != b)
        {
            parent[a] = b;
        }
    };

    // Sweep-line: sort aliasing resources by (heap_id, heap_offset) and unite overlapping ranges.
    std::vector<const PlacedResourceInfo*> sorted;
    sorted.reserve(aliasing_resource_infos_.size());
    for (const auto& [id, info] : aliasing_resource_infos_)
    {
        sorted.push_back(&info);
    }
    std::sort(sorted.begin(), sorted.end(), [](const PlacedResourceInfo* a, const PlacedResourceInfo* b) {
        return a->heap_id != b->heap_id ? a->heap_id < b->heap_id : a->heap_offset < b->heap_offset;
    });

    auto can_alias_pair = [](const PlacedResourceInfo* a, const PlacedResourceInfo* b) -> bool {
        if (a->heap_offset != b->heap_offset)
        {
            const bool a_is_tex = (a->resource_desc.Dimension != D3D12_RESOURCE_DIMENSION_BUFFER);
            const bool b_is_tex = (b->resource_desc.Dimension != D3D12_RESOURCE_DIMENSION_BUFFER);
            if (a_is_tex || b_is_tex)
            {
                return false;
            }
        }
        return true;
    };

    for (size_t i = 0; i < sorted.size(); ++i)
    {
        const PlacedResourceInfo* a = sorted[i];
        const uint64_t a_end = (a->estimated_size > 0) ? (a->heap_offset + a->estimated_size) : (a->heap_offset + 1);

        for (size_t j = i + 1; j < sorted.size(); ++j)
        {
            const PlacedResourceInfo* b = sorted[j];
            if (b->heap_id != a->heap_id)
            {
                break;
            }
            if (b->heap_offset >= a_end)
            {
                break;
            }
            if (can_alias_pair(a, b))
            {
                unite(a->resource_id, b->resource_id);
            }
        }
    }

    // Assign monotonic component IDs and build component vectors.
    uint32_t                                       next_component = 0;
    std::unordered_map<format::HandleId, uint32_t> root_to_component;
    for (const auto& [id, _] : aliasing_resource_infos_)
    {
        auto root           = find(id);
        auto [it, inserted] = root_to_component.emplace(root, next_component);
        if (inserted)
        {
            ++next_component;
        }
        aliasing_groups_[id] = it->second;
        aliasing_components_[it->second].push_back(&aliasing_resource_infos_.at(id));
    }
}

// ---------------------------------------------------------------------------
// CanOptimize / InjectAliasingMetaData
// ---------------------------------------------------------------------------

bool Dx12ResourceAliasingModifier::CanOptimize()
{
    // Detect resources that alias via heap memory+lifetime overlap but have no aliasing barrier.
    DetectImplicitAliasingResources();

    // Populate aliasing_resource_infos_ for all detected aliasing resources.
    for (const auto& id : aliasing_resource_ids_)
    {
        auto it = placed_resource_infos_.find(id);
        if (it != placed_resource_infos_.end())
        {
            aliasing_resource_infos_[id] = it->second;
        }
    }

    // Build connected components (merges barrier-detected + range-overlap resources).
    BuildAliasingComponents();

    GFXRECON_WRITE_CONSOLE("Found %zu aliasing D3D12 placed resources.", aliasing_resource_ids_.size());
    return !aliasing_resource_ids_.empty();
}

std::vector<const Dx12ResourceAliasingModifier::PlacedResourceInfo*>
Dx12ResourceAliasingModifier::GetAliasingResourceGroup(format::HandleId resource_id) const
{
    auto git = aliasing_groups_.find(resource_id);
    if (git == aliasing_groups_.end())
    {
        return {};
    }
    auto cit = aliasing_components_.find(git->second);
    if (cit == aliasing_components_.end())
    {
        return {};
    }

    auto group = cit->second;
    std::sort(group.begin(), group.end(), [](const PlacedResourceInfo* a, const PlacedResourceInfo* b) {
        return (a->creation_index != b->creation_index) ? (a->creation_index < b->creation_index)
                                                        : (a->resource_id < b->resource_id);
    });
    return group;
}

void Dx12ResourceAliasingModifier::InjectAliasingMetaData(format::HandleId resource_id)
{
    auto group = GetAliasingResourceGroup(resource_id);
    if (group.size() <= 1)
    {
        return;
    }

    auto new_call       = CreatePreCall();
    new_call->type      = util::CallModifierBase::NewCallDataType::MetaDataCall;
    new_call->object_id = resource_id;
    new_call->call_id   = format::ApiCallId::ApiCall_Unknown;
    new_call->thread_id = 0;

    format::arm::Dx12ResourceAliasingCommandHeader header{};
    header.meta_header.block_header.type = format::BlockType::kMetaDataBlock;
    header.meta_header.meta_data_id      = format::MakeMetaDataId(format::ApiFamilyId::ApiFamily_D3D12,
                                                             format::arm::MetaDataType::kDx12ResourceAliasingCommand);
    header.thread_id                     = 0;
    header.resources_count               = static_cast<uint64_t>(group.size());
    header.meta_header.block_header.size =
        format::GetMetaDataBlockBaseSize(header) + (group.size() * sizeof(format::arm::Dx12ResourceAliasingInfo));

    new_call->parameter_buffer.Write(&header, sizeof(header));
    for (const auto* resource : group)
    {
        format::arm::Dx12ResourceAliasingInfo info{};
        info.resource_id = resource->resource_id;
        info.heap_id     = resource->heap_id;
        info.heap_offset = resource->heap_offset;
        std::memcpy(info.resource_desc, &resource->resource_desc, sizeof(D3D12_RESOURCE_DESC1));
        new_call->parameter_buffer.Write(&info, sizeof(info));
    }
}

GFXRECON_END_NAMESPACE(decode)
GFXRECON_END_NAMESPACE(gfxrecon)
