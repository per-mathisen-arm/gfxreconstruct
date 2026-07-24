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

#ifndef GFXRECON_DX12_RESOURCE_ALIASING_MODIFIER_H
#define GFXRECON_DX12_RESOURCE_ALIASING_MODIFIER_H

#include "util/dx12_modifier_base.h"
#include <map>
#include <unordered_map>
#include <unordered_set>
#include <vector>

GFXRECON_BEGIN_NAMESPACE(gfxrecon)
GFXRECON_BEGIN_NAMESPACE(decode)

class Dx12ResourceAliasingModifier : public util::Dx12ModifierBase
{
  public:
    Dx12ResourceAliasingModifier()          = default;
    virtual ~Dx12ResourceAliasingModifier() = default;

    virtual bool CanOptimize() override;

    virtual void
    Process_ID3D12Device_CreatePlacedResource(const ApiCallInfo&                                 call_info,
                                              format::HandleId                                   object_id,
                                              HRESULT                                            return_value,
                                              format::HandleId                                   pHeap,
                                              UINT64                                             HeapOffset,
                                              StructPointerDecoder<Decoded_D3D12_RESOURCE_DESC>* pDesc,
                                              D3D12_RESOURCE_STATES                              InitialState,
                                              StructPointerDecoder<Decoded_D3D12_CLEAR_VALUE>*   pOptimizedClearValue,
                                              Decoded_GUID                                       riid,
                                              HandlePointerDecoder<void*>*                       ppvResource) override;

    virtual void
    Process_ID3D12Device8_CreatePlacedResource1(const ApiCallInfo&                                  call_info,
                                                format::HandleId                                    object_id,
                                                HRESULT                                             return_value,
                                                format::HandleId                                    pHeap,
                                                UINT64                                              HeapOffset,
                                                StructPointerDecoder<Decoded_D3D12_RESOURCE_DESC1>* pDesc,
                                                D3D12_RESOURCE_STATES                               InitialState,
                                                StructPointerDecoder<Decoded_D3D12_CLEAR_VALUE>* pOptimizedClearValue,
                                                Decoded_GUID                                     riid,
                                                HandlePointerDecoder<void*>*                     ppvResource) override;

    virtual void
    Process_ID3D12Device10_CreatePlacedResource2(const ApiCallInfo&                                  call_info,
                                                 format::HandleId                                    object_id,
                                                 HRESULT                                             return_value,
                                                 format::HandleId                                    pHeap,
                                                 UINT64                                              HeapOffset,
                                                 StructPointerDecoder<Decoded_D3D12_RESOURCE_DESC1>* pDesc,
                                                 D3D12_BARRIER_LAYOUT                                InitialLayout,
                                                 StructPointerDecoder<Decoded_D3D12_CLEAR_VALUE>* pOptimizedClearValue,
                                                 UINT32                                           NumCastableFormats,
                                                 PointerDecoder<DXGI_FORMAT>*                     pCastableFormats,
                                                 Decoded_GUID                                     riid,
                                                 HandlePointerDecoder<void*>*                     ppvResource) override;

    virtual void
    Process_IUnknown_Release(const ApiCallInfo& call_info, format::HandleId object_id, ULONG return_value) override;

    virtual void Process_ID3D12GraphicsCommandList_ResourceBarrier(
        const ApiCallInfo&                                    call_info,
        format::HandleId                                      object_id,
        UINT                                                  NumBarriers,
        StructPointerDecoder<Decoded_D3D12_RESOURCE_BARRIER>* pBarriers) override;

    virtual void
    ProcessDx12ResourceAliasingCommand(const format::arm::Dx12ResourceAliasingCommandHeader& command_header,
                                       const uint8_t*                                        data) override;

    // Intercept captured GetResourceAllocationInfo* calls to record resource sizes.
    virtual void Process_ID3D12Device_GetResourceAllocationInfo(
        const ApiCallInfo&                                 call_info,
        format::HandleId                                   object_id,
        Decoded_D3D12_RESOURCE_ALLOCATION_INFO             return_value,
        UINT                                               visibleMask,
        UINT                                               numResourceDescs,
        StructPointerDecoder<Decoded_D3D12_RESOURCE_DESC>* pResourceDescs) override;

    virtual void Process_ID3D12Device4_GetResourceAllocationInfo1(
        const ApiCallInfo&                                             call_info,
        format::HandleId                                               object_id,
        Decoded_D3D12_RESOURCE_ALLOCATION_INFO                         return_value,
        UINT                                                           visibleMask,
        UINT                                                           numResourceDescs,
        StructPointerDecoder<Decoded_D3D12_RESOURCE_DESC>*             pResourceDescs,
        StructPointerDecoder<Decoded_D3D12_RESOURCE_ALLOCATION_INFO1>* pResourceAllocationInfo1) override;

    virtual void Process_ID3D12Device8_GetResourceAllocationInfo2(
        const ApiCallInfo&                                             call_info,
        format::HandleId                                               object_id,
        Decoded_D3D12_RESOURCE_ALLOCATION_INFO                         return_value,
        UINT                                                           visibleMask,
        UINT                                                           numResourceDescs,
        StructPointerDecoder<Decoded_D3D12_RESOURCE_DESC1>*            pResourceDescs,
        StructPointerDecoder<Decoded_D3D12_RESOURCE_ALLOCATION_INFO1>* pResourceAllocationInfo1) override;

    virtual void Process_ID3D12Device12_GetResourceAllocationInfo3(
        const ApiCallInfo&                                             call_info,
        format::HandleId                                               object_id,
        Decoded_D3D12_RESOURCE_ALLOCATION_INFO                         return_value,
        UINT                                                           visibleMask,
        UINT                                                           numResourceDescs,
        StructPointerDecoder<Decoded_D3D12_RESOURCE_DESC1>*            pResourceDescs,
        PointerDecoder<UINT32>*                                        pNumCastableFormats,
        PointerDecoder<DXGI_FORMAT*>*                                  ppCastableFormats,
        StructPointerDecoder<Decoded_D3D12_RESOURCE_ALLOCATION_INFO1>* pResourceAllocationInfo1) override;

  private:
    struct PlacedResourceInfo
    {
        format::HandleId     resource_id{ format::kNullHandleId };
        format::HandleId     heap_id{ format::kNullHandleId };
        uint64_t             heap_offset{ 0 };
        D3D12_RESOURCE_DESC1 resource_desc{};
        uint64_t             creation_index{ 0 };
        uint64_t             destruction_index{ UINT64_MAX };
        // GPU allocation size from a captured GetResourceAllocationInfo* call; 0 = unknown.
        uint64_t estimated_size{ 0 };
    };

    // Returns canonical D3D12_RESOURCE_DESC1 bytes (padding zeroed) for use as a map key.
    static std::vector<uint8_t> MakeDescKey(const D3D12_RESOURCE_DESC1& desc);
    static std::vector<uint8_t> MakeDescKey(const D3D12_RESOURCE_DESC& desc);

    void                                   TrackCreatePlacedResource(const ApiCallInfo&           call_info,
                                                                     HRESULT                      return_value,
                                                                     format::HandleId             pHeap,
                                                                     UINT64                       HeapOffset,
                                                                     const D3D12_RESOURCE_DESC1&  resource_desc,
                                                                     HandlePointerDecoder<void*>* ppvResource);
    void                                   DetectImplicitAliasingResources();
    void                                   BuildAliasingComponents();
    std::vector<const PlacedResourceInfo*> GetAliasingResourceGroup(format::HandleId resource_id) const;
    void                                   InjectAliasingMetaData(format::HandleId resource_id);

    // desc canonical bytes -> allocation size from captured GetResourceAllocationInfo*.
    std::map<std::vector<uint8_t>, uint64_t> desc_to_allocation_size_;

    std::unordered_map<format::HandleId, PlacedResourceInfo> placed_resource_infos_;
    std::unordered_map<format::HandleId, PlacedResourceInfo> aliasing_resource_infos_;
    std::unordered_set<format::HandleId>                     aliasing_resource_ids_;

    // Populated by BuildAliasingComponents(): resource -> connected-component ID.
    std::unordered_map<format::HandleId, uint32_t>                       aliasing_groups_;
    std::unordered_map<uint32_t, std::vector<const PlacedResourceInfo*>> aliasing_components_;
};

GFXRECON_END_NAMESPACE(decode)
GFXRECON_END_NAMESPACE(gfxrecon)

#endif // GFXRECON_DX12_RESOURCE_ALIASING_MODIFIER_H