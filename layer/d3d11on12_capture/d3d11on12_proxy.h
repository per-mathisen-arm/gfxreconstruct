/*
** Copyright (c) 2026 Arm Limited and/or its affiliates <open-source-office@arm.com>
** All rights reserved
**
** Description: D3D11On12 proxy integration header for gfxrecon dxgi layer.
**
** Routes D3D11 applications through D3D11On12 (D3D11 backed by D3D12) so that
** gfxrecon's D3D12 capture layer can record API calls from D3D11 titles.
*/

#ifndef GFXRECON_D3D11ON12_PROXY_H
#define GFXRECON_D3D11ON12_PROXY_H

#include <windows.h>

// Initialize D3D11On12 proxy. Call from DllMain on DLL_PROCESS_ATTACH.
// hDLL: handle to this proxy DLL (used to compute log path).
void D3D11On12Proxy_Init(HINSTANCE hDLL);

// Tear down D3D11On12 proxy. Call from DllMain on DLL_PROCESS_DETACH.
void D3D11On12Proxy_Destroy();

// Wrap a DXGI factory pointer with a ProxyFactory that redirects D3D11 swap
// chain creation through D3D11On12 → D3D12.  The factory has already been
// through gfxrecon's capture dispatch table, so all subsequent D3D12 calls
// (CreateSwapChainForHwnd, etc.) will be captured automatically.
//
// ppFactory must point to a valid IDXGIFactory interface with refcount 1
// (as returned by CreateDXGIFactory/1/2).  On return *ppFactory is updated
// to the ProxyFactory wrapper, or left unchanged if wrapping fails.
void D3D11On12Proxy_WrapFactory(void** ppFactory);

#endif // GFXRECON_D3D11ON12_PROXY_H
