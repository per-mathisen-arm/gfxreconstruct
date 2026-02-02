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

#ifndef GFXRECON_HOOK_DXGI_H
#define GFXRECON_HOOK_DXGI_H

#include "encode/dxgi_dispatch_table.h"

#include "util/interception/real_and_mine_hook.h"

#include <Windows.h>
#include <dxgi.h>

class Hook_DXGI;

// Helper struct that holds data required for hook management
struct DxgiHookInfo
{
    // GFXR DLL with capture implementation, which is only loaded when capture is enabled
    HMODULE capture_dll = nullptr;

    // System DLL providing the DXGI API calls to wrap
    HMODULE dxgi_debug_dll = nullptr;

    // Dispatch table pointing either at the GPU or capture layer
    gfxrecon::encode::DxgiDebugDispatchTable dispatch_table = {};

    // Interceptor class in charge of hooking
    Hook_DXGI* interceptor = nullptr;
};

//-----------------------------------------------------------------------------
/// typedef function pointers
//-----------------------------------------------------------------------------
typedef HRESULT(WINAPI* PFN_DXGIGETDEBUGINTERFACE)(const IID& riid, void** pDebug);

//-----------------------------------------------------------------------------
/// This class is responsible for hooking DXGI module entry points. It contains
/// a "Mine" implementation for each hooked function.
//-----------------------------------------------------------------------------
class Hook_DXGI
{
  public:
    static Hook_DXGI* GetInterceptor();

    Hook_DXGI() {}
    virtual ~Hook_DXGI() {}

    static bool HookInterceptor(bool capture);
    static bool UnhookInterceptor();

    gfxrecon::util::interception::RealAndMineHook<PFN_DXGIGETDEBUGINTERFACE> hook_DXGIGetDebugInterface_;
};
#endif // GFXRECON_HOOK_DXGI_H
