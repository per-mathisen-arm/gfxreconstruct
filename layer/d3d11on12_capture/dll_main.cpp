/*
** Copyright (c) 2026 Arm Limited and/or its affiliates <open-source-office@arm.com>
** All rights reserved
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

#include "d3d11on12_proxy.h"

#include <windows.h>

// Exported entry point called by dxgi.dll after every successful
// CreateDXGIFactory / CreateDXGIFactory1 / CreateDXGIFactory2.
// Wraps the factory with a ProxyFactory that redirects D3D11 swap-chain
// creation through D3D11On12 → D3D12 so that gfxrecon captures the calls.
EXTERN_C void D3D11On12Capture_WrapFactory(void** ppFactory)
{
    D3D11On12Proxy_WrapFactory(ppFactory);
}

BOOL WINAPI DllMain(HINSTANCE hinstDLL, DWORD fdwReason, LPVOID lpvReserved)
{
    switch (fdwReason)
    {
        case DLL_PROCESS_ATTACH:
            D3D11On12Proxy_Init(hinstDLL);
            break;
        case DLL_PROCESS_DETACH:
            // Only clean up if the process is not terminating.  If it is
            // terminating (lpvReserved != nullptr) OS reclaims memory anyway.
            if (lpvReserved == nullptr)
                D3D11On12Proxy_Destroy();
            break;
        default:
            break;
    }
    return TRUE;
}
