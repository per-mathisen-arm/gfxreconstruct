#include "generated/generated_dx12_device_prune_consumer.h"
#include <unordered_set>
#include <unordered_map>
#include <cstdint>

GFXRECON_BEGIN_NAMESPACE(gfxrecon)
GFXRECON_BEGIN_NAMESPACE(decode)

void Dx12DevicePruneConsumer::Process_D3D12CreateDevice(const ApiCallInfo&           call_info,
                                                        HRESULT                      return_value,
                                                        format::HandleId             pAdapter,
                                                        D3D_FEATURE_LEVEL            MinimumFeatureLevel,
                                                        Decoded_GUID                 riid,
                                                        HandlePointerDecoder<void*>* ppDevice)
{
    if (return_value == S_OK && ppDevice->GetPointer() != nullptr)
    {
        GFXRECON_LOG_WARNING("Found device = %u", *ppDevice->GetPointer());
        if (target_.count(*ppDevice->GetPointer()) > 0)
        {
            GFXRECON_LOG_WARNING("Removing matching device = %u", *ppDevice->GetPointer());
            blocks_.insert(call_info.index);
        }
    }
    else
    {
        GFXRECON_LOG_WARNING("Removing unsuccessful create device, index %u", call_info.index);
        blocks_.insert(call_info.index);
    }
}

void Dx12DevicePruneConsumer::Process_ID3D12DeviceFactory_CreateDevice(const ApiCallInfo&           call_info,
                                                                       format::HandleId             object_id,
                                                                       HRESULT                      return_value,
                                                                       format::HandleId             adapter,
                                                                       D3D_FEATURE_LEVEL            FeatureLevel,
                                                                       Decoded_GUID                 riid,
                                                                       HandlePointerDecoder<void*>* ppvDevice)
{
    if (return_value == S_OK && ppvDevice->GetPointer() != nullptr)
    {
        GFXRECON_LOG_WARNING("Found device = %u", *ppvDevice->GetPointer());
        if (target_.count(*ppvDevice->GetPointer()) > 0)
        {
            GFXRECON_LOG_WARNING("Removing matching device = %u", *ppvDevice->GetPointer());
            blocks_.insert(call_info.index);
        }
    }
    else
    {
        GFXRECON_LOG_WARNING("Removing unsuccessful create device, index %u", call_info.index);
        blocks_.insert(call_info.index);
    }
}

void Dx12DevicePruneConsumer::Process_ID3D12SDKConfiguration1_CreateDeviceFactory(
    const ApiCallInfo&           call_info,
    format::HandleId             object_id,
    HRESULT                      return_value,
    UINT                         SDKVersion,
    StringDecoder*               SDKPath,
    Decoded_GUID                 riid,
    HandlePointerDecoder<void*>* ppvFactory)
{
    Mark(call_info, object_id);
}

GFXRECON_END_NAMESPACE(decode)
GFXRECON_END_NAMESPACE(gfxrecon)