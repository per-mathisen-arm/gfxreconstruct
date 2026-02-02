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

#include "hook_dxgi_debug.h"

#include "util/file_path.h"
#include "util/interception/interception_util.h"

// Static data required for hook management
static DxgiHookInfo hook_info_ = {};

HRESULT WINAPI Mine_DXGIGetDebugInterface(const IID& riid, void** pDebug)
{
    HRESULT result = S_FALSE;

    result = hook_info_.dispatch_table.DXGIGetDebugInterface(riid, pDebug);

    return result;
}

//----------------------------------------------------------------------------
/// Fill in a dispatch table with function addresses obtained from DXGIDebug.dll
///
/// \param  DXGIDebug_module Output dispatch table.
///
/// \return True if successful, false otherwise.
//----------------------------------------------------------------------------
bool GetDxgiDebugDispatchTable(gfxrecon::encode::DxgiDebugDispatchTable& dxgi_debug_table)
{
    std::string library_base_path = "";

    bool success = gfxrecon::util::filepath::GetWindowsSystemLibrariesPath(library_base_path);

    if (success == true)
    {
        std::string library_path = library_base_path + "\\DXGIDebug.dll";

        hook_info_.dxgi_debug_dll = LoadLibraryA(library_path.c_str());

        if (hook_info_.dxgi_debug_dll != nullptr)
        {
            dxgi_debug_table.DXGIGetDebugInterface = reinterpret_cast<PFN_DXGIGETDEBUGINTERFACE>(
                GetProcAddress(hook_info_.dxgi_debug_dll, "DXGIGetDebugInterface"));
            success = true;
        }
    }

    return success;
}

//----------------------------------------------------------------------------
/// Given a DXGIDebug dispatch table, perform hooking and write out the final
/// dispatch table that will be called by intercepted entry points.
///
/// \param  dxgi_debug_table  Incoming dispatch table with entry function addresses
///                     in DXGIDebug.
///
/// \param  gpu_table Outgoing dispatch table with hooked entry points that
///                   will either go to the GPU or capture layer.
///
/// \return True if successful, false otherwise.
//----------------------------------------------------------------------------
bool GetDxgiDebugDispatchTableHooked(gfxrecon::encode::DxgiDebugDispatchTable  dxgi_debug_table,
                                     gfxrecon::encode::DxgiDebugDispatchTable& gpu_table)
{
    bool success = false;

    Hook_DXGI* pInterceptor = Hook_DXGI::GetInterceptor();

    if (pInterceptor != nullptr)
    {
        bool attach_success = false;

        if (dxgi_debug_table.DXGIGetDebugInterface != nullptr)
        {
            pInterceptor->hook_DXGIGetDebugInterface_.SetHooks(dxgi_debug_table.DXGIGetDebugInterface,
                                                               Mine_DXGIGetDebugInterface);
            attach_success                  = pInterceptor->hook_DXGIGetDebugInterface_.Attach();
            gpu_table.DXGIGetDebugInterface = pInterceptor->hook_DXGIGetDebugInterface_.real_hook_;
        }

        success = true;
    }

    return success;
}

//-----------------------------------------------------------------------------
/// Get unique instance of Hook_DXGI
//-----------------------------------------------------------------------------
Hook_DXGI* Hook_DXGI::GetInterceptor()
{
    if (hook_info_.interceptor == nullptr)
    {
        hook_info_.interceptor = new Hook_DXGI();
    }

    return hook_info_.interceptor;
}

//-----------------------------------------------------------------------------
/// Attach API entry points for hooking.
///
/// \param capture Whether capture is enabled
///
/// \return True if entry points were successfully hooked.
//-----------------------------------------------------------------------------
bool Hook_DXGI::HookInterceptor(bool capture)
{
    bool success = false;

    Hook_DXGI* pInterceptor = GetInterceptor();

    if (pInterceptor != nullptr)
    {
        gfxrecon::encode::DxgiDebugDispatchTable dispatch_table_dxgi_debug = {};

        success = GetDxgiDebugDispatchTable(dispatch_table_dxgi_debug);

        if (success == true)
        {
            success = GetDxgiDebugDispatchTableHooked(dispatch_table_dxgi_debug, hook_info_.dispatch_table);
            if (success == true)
            {
                if (capture == true)
                {
                    if (hook_info_.capture_dll == nullptr)
                    {
                        const std::string gfxr_d3d12_capture_path = gfxrecon::util::interception::CaptureLibPath();

                        hook_info_.capture_dll = gfxrecon::util::platform::OpenLibrary(gfxr_d3d12_capture_path.c_str());

                        if (hook_info_.capture_dll != nullptr)
                        {
                            auto init_func = reinterpret_cast<PFN_InitializeDxgiDebugCapture>(
                                GetProcAddress(hook_info_.capture_dll, "InitializeDxgiDebugCapture"));

                            if (init_func != nullptr)
                            {
                                init_func(&hook_info_.dispatch_table);
                            }
                        }
                    }
                }
            }
        }
    }

    return success;
}

//-----------------------------------------------------------------------------
/// Detach all hooked API entry points.
/// \return True if entry points were successfully detached.
//-----------------------------------------------------------------------------
bool Hook_DXGI::UnhookInterceptor()
{
    bool success = false;

    Hook_DXGI* pInterceptor = GetInterceptor();

    if (pInterceptor != nullptr)
    {
        pInterceptor->hook_DXGIGetDebugInterface_.Detach();

        success = true;
    }

    return success;
}
