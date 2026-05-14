/*
** Copyright (c) 2026 Arm Limited and/or its affiliates <open-source-office@arm.com>
** All rights reserved
**
** Description: D3D11On12 proxy integration for gfxrecon dxgi capture layer.
**
** Routes D3D11 applications through D3D11On12 (D3D11 backed by D3D12) so that
** gfxrecon's D3D12 capture layer can record API calls from D3D11 titles.
**
** Hooks D3D11CreateDevice (IAT + inline) to redirect device creation through
** D3D11On12CreateDevice backed by a captured D3D12 device.  Also wraps DXGI
** factories (ProxyFactory) to redirect swap chain creation to D3D12 paths.
**
** Denuvo bypass: when proxy_denuvo.txt exists in the EXE directory, presents a
** "clean" D3D11 device facade that hides the internal D3D12 COM pointers from
** Denuvo's memory scan, while forwarding all virtual calls to D3D11On12.
*/

#include "d3d11on12_proxy.h"
#include <Windows.h>
#include <winternl.h>
#include <tlhelp32.h>
#include <d3d11.h>
#include <d3d11_4.h>
#include <d3d11on12.h>
#include <d3d12.h>
#include <dxgi1_6.h>
#include <wrl/client.h>
#include <algorithm>
#include <cstdio>
#include <cstdarg>
#include <exception>
#include <memory>
#include <mutex>
#include <system_error>
#include <unordered_map>
#include <vector>

using Microsoft::WRL::ComPtr;

// =============================================================================
// Runtime paths (set in DllMain; used by ProxyLog and dxgi_ms loading)
// =============================================================================

static wchar_t g_dllDir[MAX_PATH]  = {}; // directory containing this proxy DLL
static wchar_t g_exeDir[MAX_PATH]  = {}; // directory containing the host EXE
static char    g_logPath[MAX_PATH] = {}; // full path to d3d11proxy.log

// Denuvo-bypass mode: enabled when proxy_denuvo.txt exists in EXE directory.
// When false (default for all other apps), returns the D3D11On12 device directly
// without the vtable-cave/clean-device tricks that Denuvo requires.
static bool g_denuvoMode = false;

// D3D11On12 capture mode: enabled when proxy_d3d11on12.txt OR proxy_denuvo.txt
// exists in the EXE directory.  When false (the default), WrapFactory is a no-op
// so pure D3D12/Vulkan apps are unaffected by this module.
static bool g_d3d11on12Enabled = false;

// =============================================================================
// Logging
// =============================================================================

static void ProxyLog(const char* fmt, ...)
{
    if (!g_logPath[0])
        return;
    FILE* f = nullptr;
    if (fopen_s(&f, g_logPath, "a") != 0 || !f)
        return;
    va_list args;
    va_start(args, fmt);
    vfprintf(f, fmt, args);
    va_end(args);
    fputc('\n', f);
    fclose(f);
}

// Vectored exception handler: installed on first D3D11 API call so it only
// fires for D3D11 apps.  VEH fires first-chance (before any SEH/__try handler
// in the call stack), so D3D runtime's own __try/__except blocks still handle
// their own exceptions normally after VEH returns EXCEPTION_CONTINUE_SEARCH.
//
// We only log exceptions we haven't filtered; filtered ones are silently passed
// through so the process continues normally.
static const DWORD kExcCppException  = 0xE06D7363; // MSVC C++ throw (handled by runtime)
static const DWORD kExcSetThreadName = 0x406D1388; // SetThreadName debugger notification
static const DWORD kExcDbgPrintAnsi  = 0x40010005; // OutputDebugStringA
static const DWORD kExcDbgPrintWide  = 0x40010006; // OutputDebugStringW

static const int kCrashMaxStackFrames = 8;

static LONG WINAPI ProxyVEH(EXCEPTION_POINTERS* ep)
{
    if (!ep || !ep->ExceptionRecord || !ep->ContextRecord)
        return EXCEPTION_CONTINUE_SEARCH;
    DWORD code = ep->ExceptionRecord->ExceptionCode;
    // Skip well-known non-crash codes that fire constantly during normal execution.
    if (code == kExcCppException || code == kExcSetThreadName || code == kExcDbgPrintAnsi || code == kExcDbgPrintWide)
        return EXCEPTION_CONTINUE_SEARCH;
    // Skip near-null access violations: these are routine D3D/gfxrecon internal
    // null-checks (e.g. probing offset 0x8 of a COM pointer) that are always
    // caught by __try/__except blocks in the D3D runtime after VEH returns.
    if (code == EXCEPTION_ACCESS_VIOLATION && ep->ExceptionRecord->NumberParameters >= 2 &&
        ep->ExceptionRecord->ExceptionInformation[1] < 0x10000)
        return EXCEPTION_CONTINUE_SEARCH;

    PEXCEPTION_RECORD er  = ep->ExceptionRecord;
    PCONTEXT          ctx = ep->ContextRecord;
#if defined(_M_X64)
    uintptr_t pc   = ctx->Rip;
    uintptr_t sp   = ctx->Rsp;
    uintptr_t arg0 = ctx->Rcx;
    uintptr_t arg1 = ctx->Rdx;
#elif defined(_M_ARM64)
    uintptr_t pc   = ctx->Pc;
    uintptr_t sp   = ctx->Sp;
    uintptr_t arg0 = ctx->X0;
    uintptr_t arg1 = ctx->X1;
#else
    uintptr_t pc   = 0;
    uintptr_t sp   = 0;
    uintptr_t arg0 = 0;
    uintptr_t arg1 = 0;
#endif
    ProxyLog("[CRASH] code=0x%08X addr=%p PC=%p SP=%p arg0=%p arg1=%p",
             er->ExceptionCode,
             er->ExceptionAddress,
             (void*)pc,
             (void*)sp,
             (void*)arg0,
             (void*)arg1);
    for (DWORD i = 0; i < er->NumberParameters; i++)
        ProxyLog("[CRASH]   param[%u]=0x%016llX", i, (unsigned long long)er->ExceptionInformation[i]);
    for (int fi = 0; fi < kCrashMaxStackFrames; fi++)
    {
        if (IsBadReadPtr(reinterpret_cast<const void*>(sp + fi * sizeof(uintptr_t)), sizeof(uintptr_t)))
            break;
        uintptr_t ra = *reinterpret_cast<uintptr_t*>(sp + fi * sizeof(uintptr_t));
        ProxyLog("[CRASH]   [SP+%02d*8] = %p", fi, (void*)ra);
    }
    return EXCEPTION_CONTINUE_SEARCH;
}

// Global present counter for all ProxySwapChain instances (diagnostic).
static volatile LONG g_totalPresentCount = 0;

static PVOID g_vehHandle = nullptr;

// Custom std::terminate handler: when an exception escapes a noexcept boundary
// (e.g. std::system_error from D3D12Core/dxgi_ms propagating into the game's
// noexcept code), the default handler calls __fastfail which bypasses all user-mode
// handlers and terminates without flushing gfxrecon's capture buffers.
// ExitProcess(0) instead allows DLL_PROCESS_DETACH to run and flush the trace.
static void ProxyTerminateHandler()
{
    ProxyLog("[D3D11On12Proxy] std::terminate intercepted (presents=%ld) - flushing capture and exiting",
             g_totalPresentCount);

    // Log a stack backtrace to help diagnose the throw site.
    void*  frames[24] = {};
    USHORT count      = RtlCaptureStackBackTrace(0, 24, frames, nullptr);
    for (USHORT i = 0; i < count; ++i)
    {
        HMODULE hMod              = nullptr;
        char    modName[MAX_PATH] = "<unknown>";
        if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                               reinterpret_cast<LPCSTR>(frames[i]),
                               &hMod) &&
            hMod)
        {
            GetModuleFileNameA(hMod, modName, MAX_PATH);
        }
        ProxyLog("[terminate] [%02u] %p  %s+0x%llx",
                 (unsigned)i,
                 frames[i],
                 modName,
                 (unsigned long long)((BYTE*)frames[i] - (BYTE*)hMod));
    }

    ExitProcess(0);
}

static void InstallCrashFilter()
{
    if (!g_vehHandle)
        g_vehHandle = AddVectoredExceptionHandler(1, ProxyVEH);
    std::set_terminate(ProxyTerminateHandler);
}

// =============================================================================
// D3D12 / D3D11On12 function pointer types and globals
// =============================================================================

typedef HRESULT(WINAPI* PFN_D3D12CreateDevice_t)(IUnknown*, D3D_FEATURE_LEVEL, REFIID, void**);

typedef HRESULT(WINAPI* PFN_D3D11On12CreateDevice_t)(IUnknown*,
                                                     UINT,
                                                     const D3D_FEATURE_LEVEL*,
                                                     UINT,
                                                     IUnknown* const*,
                                                     UINT,
                                                     UINT,
                                                     ID3D11Device**,
                                                     ID3D11DeviceContext**,
                                                     D3D_FEATURE_LEVEL*);

typedef HRESULT(WINAPI* PFN_D3D11CreateDevice_t)(IDXGIAdapter*,
                                                 D3D_DRIVER_TYPE,
                                                 HMODULE,
                                                 UINT,
                                                 const D3D_FEATURE_LEVEL*,
                                                 UINT,
                                                 UINT,
                                                 ID3D11Device**,
                                                 D3D_FEATURE_LEVEL*,
                                                 ID3D11DeviceContext**);

static HMODULE                     g_hD3D12            = nullptr;
static HMODULE                     g_hD3D11Sys         = nullptr;
static PFN_D3D12CreateDevice_t     g_D3D12CreateDevice = nullptr;
static PFN_D3D11On12CreateDevice_t g_D3D11On12Create   = nullptr;

// Original D3D11CreateDevice pointer saved during IAT patching.
static PFN_D3D11CreateDevice_t g_iatOrig_D3D11Create = nullptr;

// Per-thunk patch record (one entry per patched IAT slot across all modules).
struct PatchedThunk
{
    PIMAGE_THUNK_DATA thunk;
    ULONGLONG         origAddr;
};
static std::vector<PatchedThunk> g_patchedThunks;

static thread_local bool g_creatingDevice = false;
static bool              g_iatPatched     = false;
static SRWLOCK           g_iatLock        = SRWLOCK_INIT;

// =============================================================================
// Named constants
// =============================================================================

// x64 indirect JMP hook: FF 25 00 00 00 00 <8-byte address> = 14 bytes
static const int kHookBytes = 14;

// x86-64 NOP opcode used to pad overwritten prologues
static const BYTE kNopByte = 0x90;

// Buffer size for saved original prologue bytes (must exceed any realistic N)
static const int kHookOrigBytesSize = 32;

// IDXGIFactory / IDXGIFactory2 vtable slot indices (0-based)
static const int kFactoryCreateSwapChainVtIdx        = 10; // IDXGIFactory::CreateSwapChain
static const int kFactoryCreateSwapChainForHwndVtIdx = 15; // IDXGIFactory2::CreateSwapChainForHwnd

// Forwarding stub size: MOV RCX,imm64 (10) + JMP [RIP+0] opcode+offset (6) + target ptr (8)
static const int kForwardStubBytes = 24;

// ID3D11Device vtable slot for GetImmediateContext
static const int kDevGetImmCtxVtIdx = 40;

// PE IMAGE_SECTION_HEADER.Name is always 8 bytes (not null-terminated)
static const int kImageSectionNameLen = 8;

// Pool sizes for executable stubs and thunks (bytes)
static const SIZE_T kStubPoolSize  = 64 * 1024;
static const SIZE_T kThunkPoolSize = 16 * 1024;

// =============================================================================
// Per-device D3D12 context (maps D3D11 device -> D3D12 command queue, etc.)
// =============================================================================

struct DeviceContext
{
    ComPtr<ID3D12Device>        d3d12Device;
    ComPtr<ID3D12CommandQueue>  cmdQueue;
    ComPtr<ID3D11On12Device>    d3d11On12Device;
    ComPtr<ID3D11DeviceContext> d3d11Context;
};

static std::unordered_map<ID3D11Device*, std::shared_ptr<DeviceContext>> g_deviceContexts;
static SRWLOCK                                                           g_deviceContextLock = SRWLOCK_INIT;

// =============================================================================
// Helper: convert pDesc1 back to a DXGI_SWAP_CHAIN_DESC for GetDesc compat.
// =============================================================================

static DXGI_SWAP_CHAIN_DESC
BuildLegacyDesc(HWND hwnd, const DXGI_SWAP_CHAIN_DESC1* pDesc1, const DXGI_SWAP_CHAIN_FULLSCREEN_DESC* pFsDesc)
{
    DXGI_SWAP_CHAIN_DESC d = {};
    d.BufferDesc.Width     = pDesc1->Width;
    d.BufferDesc.Height    = pDesc1->Height;
    d.BufferDesc.Format    = pDesc1->Format;
    d.SampleDesc           = pDesc1->SampleDesc;
    d.BufferUsage          = pDesc1->BufferUsage;
    d.BufferCount          = pDesc1->BufferCount;
    d.OutputWindow         = hwnd;
    d.Windowed             = (pFsDesc == nullptr) ? TRUE : FALSE;
    d.SwapEffect           = pDesc1->SwapEffect;
    d.Flags                = pDesc1->Flags;
    if (pFsDesc)
    {
        d.BufferDesc.RefreshRate      = pFsDesc->RefreshRate;
        d.BufferDesc.ScanlineOrdering = pFsDesc->ScanlineOrdering;
        d.BufferDesc.Scaling          = pFsDesc->Scaling;
    }
    return d;
}

// =============================================================================
// Helper: convert old DXGI_SWAP_CHAIN_DESC to desc1 suitable for D3D12.
// =============================================================================

static void ConvertDesc(const DXGI_SWAP_CHAIN_DESC& src, DXGI_SWAP_CHAIN_DESC1& desc1)
{
    desc1           = {};
    desc1.Width     = src.BufferDesc.Width;
    desc1.Height    = src.BufferDesc.Height;
    DXGI_FORMAT fmt = src.BufferDesc.Format;
    if (fmt == DXGI_FORMAT_B8G8R8A8_UNORM_SRGB)
        fmt = DXGI_FORMAT_B8G8R8A8_UNORM;
    else if (fmt == DXGI_FORMAT_R8G8B8A8_UNORM_SRGB)
        fmt = DXGI_FORMAT_R8G8B8A8_UNORM;
    desc1.Format      = fmt;
    desc1.Stereo      = FALSE;
    desc1.SampleDesc  = { 1, 0 };
    desc1.BufferUsage = src.BufferUsage | DXGI_USAGE_RENDER_TARGET_OUTPUT;
    desc1.BufferCount = (std::max)(src.BufferCount, 2u);
    desc1.Scaling     = DXGI_SCALING_STRETCH;
    desc1.SwapEffect  = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    desc1.AlphaMode   = DXGI_ALPHA_MODE_UNSPECIFIED;
    desc1.Flags       = src.Flags & ~DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;
}

struct ScopedCriticalSection
{
    explicit ScopedCriticalSection(CRITICAL_SECTION* cs) : cs_(cs) { EnterCriticalSection(cs_); }
    ~ScopedCriticalSection() { LeaveCriticalSection(cs_); }

  private:
    CRITICAL_SECTION* cs_;
};

// =============================================================================
// ProxySwapChain: wraps a D3D12 IDXGISwapChain3 and implements the
// "Virtual Back Buffer" (VBB) pattern so the game can use D3D11 BITBLT-style
// rendering: game always draws to the same VBB texture, and on Present we copy
// it to the current D3D12 back buffer.
// =============================================================================

class ProxySwapChain final : public IDXGISwapChain4
{
    volatile LONG               m_refCount{ 1 };
    ComPtr<IDXGISwapChain3>     m_real;  // underlying D3D12 swap chain
    ComPtr<ID3D11Device>        m_11dev; // D3D11On12 device
    ComPtr<ID3D11DeviceContext> m_11ctx;
    ComPtr<ID3D11On12Device>    m_11on12;
    ComPtr<ID3D12CommandQueue>  m_cmdQueue;
    DXGI_SWAP_CHAIN_DESC        m_legacyDesc; // for GetDesc()
    DXGI_SWAP_CHAIN_DESC1       m_desc1;
    CRITICAL_SECTION            m_swapChainLock{};
    UINT                        m_presentCount{ 0 };
    bool                        m_deactivated{ false };

    // Virtual back buffer (VBB) - game always renders here
    ComPtr<ID3D11Texture2D>        m_vbb;
    ComPtr<ID3D11RenderTargetView> m_vbbRTV;

    // D3D11On12 wrapped back buffers (one per real back buffer)
    std::vector<ComPtr<ID3D11Resource>> m_wrappedBackBuffers;

    void InitWrappedBackBuffers()
    {
        m_wrappedBackBuffers.clear();
        DXGI_SWAP_CHAIN_DESC1 d = {};
        m_real->GetDesc1(&d);
        m_wrappedBackBuffers.resize(d.BufferCount);
        D3D11_RESOURCE_FLAGS rf = { D3D11_BIND_RENDER_TARGET };
        for (UINT i = 0; i < d.BufferCount; ++i)
        {
            ComPtr<ID3D12Resource> buf;
            HRESULT                hr12 = m_real->GetBuffer(i, IID_PPV_ARGS(&buf));
            if (SUCCEEDED(hr12))
            {
                // D3D11On12 pattern: wrap each D3D12 swap chain buffer as a D3D11
                // resource.  InState=RENDER_TARGET (state when D3D11 uses it),
                // OutState=PRESENT (state restored after ReleaseWrappedResources).
                m_11on12->CreateWrappedResource(buf.Get(),
                                                &rf,
                                                D3D12_RESOURCE_STATE_RENDER_TARGET,
                                                D3D12_RESOURCE_STATE_PRESENT,
                                                IID_PPV_ARGS(&m_wrappedBackBuffers[i]));
            }
        }
    }

    // Create the VBB texture lazily (called from GetBuffer, NOT from the constructor).
    // D3D11On12 CreateTexture2D crashes when called from inside the CSCFH callback
    // (gfxrecon holds an internal lock / D3D12 is in "pending swap chain" state).
    // By the time the game calls GetBuffer, we are fully out of that call stack.
    void EnsureVBB()
    {
        if (m_vbb)
            return; // already created

        // Use the D3D11On12 device's ID3D11Device interface directly, bypassing
        // the cave vtable forwarding stubs (which are only needed for Denuvo compat
        // when the game is calling -- internal helper code uses the real interface).
        ComPtr<ID3D11Device> d3d11dev;
        if (FAILED(m_11on12->QueryInterface(IID_PPV_ARGS(&d3d11dev))))
            return;

        D3D11_TEXTURE2D_DESC td = {};
        td.Width                = m_desc1.Width;
        td.Height               = m_desc1.Height;
        td.MipLevels            = 1;
        td.ArraySize            = 1;
        td.Format               = m_desc1.Format;
        td.SampleDesc           = { 1, 0 };
        td.Usage                = D3D11_USAGE_DEFAULT;
        td.BindFlags            = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
        HRESULT hr              = d3d11dev->CreateTexture2D(&td, nullptr, &m_vbb);
        if (FAILED(hr))
        {
            ProxyLog("[ProxySwapChain] CreateTexture2D %ux%u hr=0x%08X", td.Width, td.Height, (unsigned)hr);
            m_vbb.Reset();
            return;
        }

        D3D11_RENDER_TARGET_VIEW_DESC rtd = {};
        rtd.Format                        = m_desc1.Format;
        rtd.ViewDimension                 = D3D11_RTV_DIMENSION_TEXTURE2D;
        d3d11dev->CreateRenderTargetView(m_vbb.Get(), &rtd, &m_vbbRTV);

        // Also init wrapped back buffers for copy-at-present.
        if (m_wrappedBackBuffers.empty())
            InitWrappedBackBuffers();
    }

    void FlushAndClearSwapChainBuffers()
    {
        if (m_11ctx)
            m_11ctx->Flush();
        m_wrappedBackBuffers.clear();
        m_vbb.Reset();
        m_vbbRTV.Reset();
    }

    HRESULT UpdateAfterResize(HRESULT hr, UINT Width, UINT Height, DXGI_FORMAT Format)
    {
        if (SUCCEEDED(hr))
        {
            if (Width)
                m_legacyDesc.BufferDesc.Width = Width;
            if (Height)
                m_legacyDesc.BufferDesc.Height = Height;
            if (Format)
                m_legacyDesc.BufferDesc.Format = Format;
            // Update desc1 dimensions so EnsureVBB re-creates at new size.
            if (Width)
                m_desc1.Width = Width;
            if (Height)
                m_desc1.Height = Height;
            if (Format)
                m_desc1.Format = Format;
        }
        // VBB will be re-created lazily on the next GetBuffer call.
        m_vbb.Reset();
        m_vbbRTV.Reset();
        return hr;
    }

    // Fill zero Width/Height from the HWND client rect.
    // DXGI interprets 0 as "use current window size", but gfxrecon captures the raw 0 values.
    // During replay, the capture environment may not have a properly sized window at that moment,
    // causing the swap chain to be recreated at 0x0. By supplying explicit dimensions here
    // we ensure gfxrecon records the real size.
    void ResolveZeroDimensions(UINT& Width, UINT& Height)
    {
        if ((Width == 0 || Height == 0) && m_legacyDesc.OutputWindow)
        {
            RECT rc = {};
            if (GetClientRect(m_legacyDesc.OutputWindow, &rc))
            {
                if (Width == 0)
                    Width = static_cast<UINT>(rc.right - rc.left);
                if (Height == 0)
                    Height = static_cast<UINT>(rc.bottom - rc.top);
            }
        }
    }

  public:
    ProxySwapChain(IDXGISwapChain3*             pReal,
                   ID3D11Device*                p11Dev,
                   ID3D11DeviceContext*         p11Ctx,
                   ID3D11On12Device*            p11on12,
                   ID3D12CommandQueue*          pCmdQueue,
                   const DXGI_SWAP_CHAIN_DESC&  legacyDesc,
                   const DXGI_SWAP_CHAIN_DESC1& desc1) :
        m_real(pReal),
        m_11dev(p11Dev), m_11ctx(p11Ctx), m_11on12(p11on12), m_cmdQueue(pCmdQueue), m_legacyDesc(legacyDesc),
        m_desc1(desc1)
    {
        InitializeCriticalSection(&m_swapChainLock);
        // VBB is created lazily on first GetBuffer -- do NOT call EnsureVBB here.
        // Creating D3D11 resources inside the CSCFH callback (where the constructor
        // is called from) causes a crash in D3D11On12 CreateTexture2D.
    }

    ~ProxySwapChain()
    {
        if (m_11ctx)
            m_11ctx->Flush();
        m_wrappedBackBuffers.clear();
        DeleteCriticalSection(&m_swapChainLock);
    }

    // Release all D3D12 resources so the command queue can accept a new swap chain.
    // Called when a newer ProxySwapChain replaces this one on the same command queue.
    // After deactivation this object is still alive (the app may hold a reference) but
    // all method calls become no-ops / return success so the app does not crash.
    void Deactivate()
    {
        ScopedCriticalSection lock(&m_swapChainLock);
        if (m_deactivated)
            return;
        m_deactivated = true;
        // Flush outstanding D3D11On12 work before releasing resources.
        FlushAndClearSwapChainBuffers();
        // Release the D3D12 swap chain so D3D12 does not see two swap chains on one queue.
        m_real.Reset();
        m_cmdQueue.Reset();
        m_11on12.Reset();
        m_11ctx.Reset();
        m_11dev.Reset();
        ProxyLog("[ProxySwapChain] Deactivated proxy=%p", (void*)this);
    }

    // -------------------------------------------------------------------------
    // IUnknown
    // -------------------------------------------------------------------------

    ULONG STDMETHODCALLTYPE AddRef() override { return InterlockedIncrement(&m_refCount); }

    ULONG STDMETHODCALLTYPE Release() override
    {
        ULONG r = InterlockedDecrement(&m_refCount);
        if (r == 0)
            delete this;
        return r;
    }

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppv) override
    {
        if (!ppv)
            return E_POINTER;
        if (riid == __uuidof(IUnknown) || riid == __uuidof(IDXGIObject) || riid == __uuidof(IDXGIDeviceSubObject) ||
            riid == __uuidof(IDXGISwapChain) || riid == __uuidof(IDXGISwapChain1) ||
            riid == __uuidof(IDXGISwapChain2) || riid == __uuidof(IDXGISwapChain3) || riid == __uuidof(IDXGISwapChain4))
        {
            *ppv = static_cast<IDXGISwapChain4*>(this);
            AddRef();
            return S_OK;
        }
        return m_real->QueryInterface(riid, ppv);
    }

    // -------------------------------------------------------------------------
    // IDXGIObject
    // -------------------------------------------------------------------------

    HRESULT STDMETHODCALLTYPE SetPrivateData(REFGUID N, UINT S, const void* D) override
    {
        return m_real->SetPrivateData(N, S, D);
    }
    HRESULT STDMETHODCALLTYPE SetPrivateDataInterface(REFGUID N, const IUnknown* U) override
    {
        return m_real->SetPrivateDataInterface(N, U);
    }
    HRESULT STDMETHODCALLTYPE GetPrivateData(REFGUID N, UINT* S, void* D) override
    {
        return m_real->GetPrivateData(N, S, D);
    }
    HRESULT STDMETHODCALLTYPE GetParent(REFIID riid, void** ppP) override { return m_real->GetParent(riid, ppP); }

    // -------------------------------------------------------------------------
    // IDXGIDeviceSubObject
    // -------------------------------------------------------------------------

    HRESULT STDMETHODCALLTYPE GetDevice(REFIID riid, void** ppDevice) override
    {
        // Game calls GetDevice(IID_ID3D11Device) -- return the D3D11On12 device.
        if (riid == __uuidof(ID3D11Device))
            return m_11dev->QueryInterface(riid, ppDevice);
        return m_real->GetDevice(riid, ppDevice);
    }

    // -------------------------------------------------------------------------
    // IDXGISwapChain
    // -------------------------------------------------------------------------

    HRESULT STDMETHODCALLTYPE Present(UINT SyncInterval, UINT Flags) override
    {
        ScopedCriticalSection lock(&m_swapChainLock);

        if (m_deactivated || !m_real)
            return S_OK;

        if (Flags & DXGI_PRESENT_TEST)
            return m_real->Present(SyncInterval, Flags);

        // Log first present to confirm rendering started.
        if (m_presentCount == 0)
            ProxyLog("[ProxySwapChain] first Present proxy=%p", (void*)this);
        ++m_presentCount;
        InterlockedIncrement(&g_totalPresentCount);

        if (m_vbb)
        {
            // Lazy-init wrapped back buffers on the first Present so ResizeBuffers
            // can succeed (no D3D11On12 wrappers alive at that point).
            if (m_wrappedBackBuffers.empty())
                InitWrappedBackBuffers();

            if (!m_wrappedBackBuffers.empty())
            {
                UINT idx = m_real->GetCurrentBackBufferIndex();
                if (idx < m_wrappedBackBuffers.size() && m_wrappedBackBuffers[idx])
                {
                    // Wrap D3D11On12 operations: gfxrecon's ExecuteCommandLists hook
                    // can throw std::system_error when D3D11On12's internally-created
                    // command lists are submitted.  Catch here to prevent the exception
                    // from propagating into the game's noexcept call stack.
                    try
                    {
                        ID3D11Resource* wrapped = m_wrappedBackBuffers[idx].Get();
                        m_11on12->AcquireWrappedResources(&wrapped, 1);
                        m_11ctx->CopyResource(wrapped, m_vbb.Get());
                        m_11on12->ReleaseWrappedResources(&wrapped, 1);
                        m_11ctx->Flush();
                    }
                    catch (const std::system_error& e)
                    {
                        ProxyLog("[ProxySwapChain] D3D11On12 copy error: %s", e.what());
                    }
                    catch (...)
                    {
                        ProxyLog("[ProxySwapChain] D3D11On12 copy exception");
                    }
                }
            }
        }
        // Wrap the D3D12 Present to prevent internal exceptions (e.g. std::system_error
        // from dxgi_ms.dll / D3D12Core.dll) from propagating into the game's noexcept
        // call stack and triggering std::terminate().
        try
        {
            return m_real->Present(SyncInterval, Flags);
        }
        catch (const std::system_error& e)
        {
            ProxyLog("[ProxySwapChain] Present system_error: %s (code=%d)", e.what(), e.code().value());
            return S_OK;
        }
        catch (const std::exception& e)
        {
            ProxyLog("[ProxySwapChain] Present exception: %s", e.what());
            return S_OK;
        }
        catch (...)
        {
            ProxyLog("[ProxySwapChain] Present unknown exception");
            return S_OK;
        }
    }

    HRESULT STDMETHODCALLTYPE GetBuffer(UINT Buffer, REFIID riid, void** ppSurface) override
    {
        ScopedCriticalSection lock(&m_swapChainLock);
        if (m_deactivated || !m_real)
            return DXGI_ERROR_INVALID_CALL;
        // Lazily create the VBB on the first GetBuffer call, now that we are
        // completely outside the MakeProxySwapChain / CSCFH call stack.
        EnsureVBB();

        // Return the VBB (a full D3D11 texture supporting ID3D11Texture2D,
        // IDXGISurface, IDXGIResource, etc.) so the game can use D3D11 rendering.
        if (m_vbb)
        {
            HRESULT hr = m_vbb->QueryInterface(riid, ppSurface);
            if (SUCCEEDED(hr))
                return hr;
        }
        // Fallback to the real D3D12 swap chain (only supports IID_ID3D12Resource).
        return m_real->GetBuffer(Buffer, riid, ppSurface);
    }

    HRESULT STDMETHODCALLTYPE SetFullscreenState(BOOL Fullscreen, IDXGIOutput* pTarget) override
    {
        return m_real->SetFullscreenState(Fullscreen, pTarget);
    }
    HRESULT STDMETHODCALLTYPE GetFullscreenState(BOOL* pFullscreen, IDXGIOutput** ppTarget) override
    {
        return m_real->GetFullscreenState(pFullscreen, ppTarget);
    }
    HRESULT STDMETHODCALLTYPE GetDesc(DXGI_SWAP_CHAIN_DESC* pDesc) override
    {
        if (!pDesc)
            return E_INVALIDARG;
        *pDesc = m_legacyDesc;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE
    ResizeBuffers(UINT BufferCount, UINT Width, UINT Height, DXGI_FORMAT Format, UINT Flags) override
    {
        ScopedCriticalSection lock(&m_swapChainLock);
        // Fill in explicit dimensions so gfxrecon captures real size rather than 0x0.
        // DXGI treats 0 as "use window size", but the replay environment may not have
        // a properly sized window yet when it re-executes this call.
        ResolveZeroDimensions(Width, Height);
        FlushAndClearSwapChainBuffers();
        HRESULT hr = m_real->ResizeBuffers(BufferCount, Width, Height, Format, Flags);
        if (FAILED(hr))
            ProxyLog("[ProxySwapChain] ResizeBuffers hr=0x%08X", (unsigned)hr);
        return UpdateAfterResize(hr, Width, Height, Format);
    }
    HRESULT STDMETHODCALLTYPE ResizeTarget(const DXGI_MODE_DESC* pNewTarget) override
    {
        return m_real->ResizeTarget(pNewTarget);
    }
    HRESULT STDMETHODCALLTYPE GetContainingOutput(IDXGIOutput** ppOutput) override
    {
        return m_real->GetContainingOutput(ppOutput);
    }
    HRESULT STDMETHODCALLTYPE GetFrameStatistics(DXGI_FRAME_STATISTICS* pStats) override
    {
        return m_real->GetFrameStatistics(pStats);
    }
    HRESULT STDMETHODCALLTYPE GetLastPresentCount(UINT* pLastPresentCount) override
    {
        return m_real->GetLastPresentCount(pLastPresentCount);
    }

    // -------------------------------------------------------------------------
    // IDXGISwapChain1
    // -------------------------------------------------------------------------

    HRESULT STDMETHODCALLTYPE GetDesc1(DXGI_SWAP_CHAIN_DESC1* pDesc) override
    {
        if (!pDesc)
            return E_INVALIDARG;
        *pDesc = m_desc1;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE GetFullscreenDesc(DXGI_SWAP_CHAIN_FULLSCREEN_DESC* pDesc) override
    {
        return m_real->GetFullscreenDesc(pDesc);
    }
    HRESULT STDMETHODCALLTYPE GetHwnd(HWND* pHwnd) override { return m_real->GetHwnd(pHwnd); }
    HRESULT STDMETHODCALLTYPE GetCoreWindow(REFIID riid, void** ppUnk) override
    {
        return m_real->GetCoreWindow(riid, ppUnk);
    }
    HRESULT STDMETHODCALLTYPE Present1(UINT                           SyncInterval,
                                       UINT                           PresentFlags,
                                       const DXGI_PRESENT_PARAMETERS* pPresentParameters) override
    {
        return Present(SyncInterval, PresentFlags);
    }
    BOOL STDMETHODCALLTYPE    IsTemporaryMonoSupported() override { return m_real->IsTemporaryMonoSupported(); }
    HRESULT STDMETHODCALLTYPE GetRestrictToOutput(IDXGIOutput** ppRestrictToOutput) override
    {
        return m_real->GetRestrictToOutput(ppRestrictToOutput);
    }
    HRESULT STDMETHODCALLTYPE SetBackgroundColor(const DXGI_RGBA* pColor) override
    {
        return m_real->SetBackgroundColor(pColor);
    }
    HRESULT STDMETHODCALLTYPE GetBackgroundColor(DXGI_RGBA* pColor) override
    {
        return m_real->GetBackgroundColor(pColor);
    }
    HRESULT STDMETHODCALLTYPE SetRotation(DXGI_MODE_ROTATION Rotation) override
    {
        return m_real->SetRotation(Rotation);
    }
    HRESULT STDMETHODCALLTYPE GetRotation(DXGI_MODE_ROTATION* pRotation) override
    {
        return m_real->GetRotation(pRotation);
    }

    // -------------------------------------------------------------------------
    // IDXGISwapChain2
    // -------------------------------------------------------------------------

    HRESULT STDMETHODCALLTYPE SetSourceSize(UINT Width, UINT Height) override
    {
        return m_real->SetSourceSize(Width, Height);
    }
    HRESULT STDMETHODCALLTYPE GetSourceSize(UINT* pWidth, UINT* pHeight) override
    {
        return m_real->GetSourceSize(pWidth, pHeight);
    }
    HRESULT STDMETHODCALLTYPE SetMaximumFrameLatency(UINT MaxLatency) override
    {
        return m_real->SetMaximumFrameLatency(MaxLatency);
    }
    HRESULT STDMETHODCALLTYPE GetMaximumFrameLatency(UINT* pMaxLatency) override
    {
        return m_real->GetMaximumFrameLatency(pMaxLatency);
    }
    HANDLE STDMETHODCALLTYPE GetFrameLatencyWaitableObject() override
    {
        return m_real->GetFrameLatencyWaitableObject();
    }
    HRESULT STDMETHODCALLTYPE SetMatrixTransform(const DXGI_MATRIX_3X2_F* pMatrix) override
    {
        return m_real->SetMatrixTransform(pMatrix);
    }
    HRESULT STDMETHODCALLTYPE GetMatrixTransform(DXGI_MATRIX_3X2_F* pMatrix) override
    {
        return m_real->GetMatrixTransform(pMatrix);
    }

    // -------------------------------------------------------------------------
    // IDXGISwapChain3
    // -------------------------------------------------------------------------

    UINT STDMETHODCALLTYPE    GetCurrentBackBufferIndex() override { return m_real->GetCurrentBackBufferIndex(); }
    HRESULT STDMETHODCALLTYPE CheckColorSpaceSupport(DXGI_COLOR_SPACE_TYPE ColorSpace,
                                                     UINT*                 pColorSpaceSupport) override
    {
        return m_real->CheckColorSpaceSupport(ColorSpace, pColorSpaceSupport);
    }
    HRESULT STDMETHODCALLTYPE SetColorSpace1(DXGI_COLOR_SPACE_TYPE ColorSpace) override
    {
        return m_real->SetColorSpace1(ColorSpace);
    }
    HRESULT STDMETHODCALLTYPE ResizeBuffers1(UINT             BufferCount,
                                             UINT             Width,
                                             UINT             Height,
                                             DXGI_FORMAT      Format,
                                             UINT             Flags,
                                             const UINT*      pCreationNodeMask,
                                             IUnknown* const* ppPresentQueue) override
    {
        ScopedCriticalSection lock(&m_swapChainLock);
        ResolveZeroDimensions(Width, Height);
        FlushAndClearSwapChainBuffers();
        HRESULT hr =
            m_real->ResizeBuffers1(BufferCount, Width, Height, Format, Flags, pCreationNodeMask, ppPresentQueue);
        if (FAILED(hr))
            ProxyLog("[ProxySwapChain] ResizeBuffers1 hr=0x%08X", (unsigned)hr);
        return UpdateAfterResize(hr, Width, Height, Format);
    }

    // -------------------------------------------------------------------------
    // IDXGISwapChain4
    // -------------------------------------------------------------------------

    HRESULT STDMETHODCALLTYPE SetHDRMetaData(DXGI_HDR_METADATA_TYPE Type, UINT Size, void* pMetaData) override
    {
        ComPtr<IDXGISwapChain4> sc4;
        if (SUCCEEDED(m_real->QueryInterface(IID_PPV_ARGS(&sc4))))
            return sc4->SetHDRMetaData(Type, Size, pMetaData);
        return E_NOINTERFACE;
    }
};

// =============================================================================
// Lookup D3D12 context for a D3D11 device
// =============================================================================

static std::shared_ptr<DeviceContext> LookupDeviceContext(ID3D11Device* pDev)
{
    AcquireSRWLockShared(&g_deviceContextLock);
    auto                           it  = g_deviceContexts.find(pDev);
    std::shared_ptr<DeviceContext> ctx = (it != g_deviceContexts.end()) ? it->second : nullptr;
    ReleaseSRWLockShared(&g_deviceContextLock);
    return ctx;
}

// Saved gfxrecon capture factory -- used as pRealFactory in MakeProxySwapChain
// so the D3D12 swap chain is created through the gfxrecon capture layer.
static ComPtr<IDXGIFactory2> g_captureFactory;

// Internal reference to the active ProxySwapChain.
// Real D3D11 swap chains are kept alive by d3d11.dll's internal reference even
// after the app releases all of its references.  We mimic that behaviour here:
// survives the app's Release() call and remains accessible via the stored pointer.
static ProxySwapChain* g_activeProxy = nullptr;

// Forward declaration -- defined after ProxySwapChain.
static HRESULT MakeProxySwapChain(IUnknown*                              pDevice,
                                  IDXGIFactory2*                         pRealFactory,
                                  HWND                                   hwnd,
                                  const DXGI_SWAP_CHAIN_DESC1*           pDesc1,
                                  const DXGI_SWAP_CHAIN_FULLSCREEN_DESC* pFsDesc,
                                  IDXGIOutput*                           pRestrictToOutput,
                                  const DXGI_SWAP_CHAIN_DESC*            pOriginalDesc,
                                  IDXGISwapChain1**                      ppSwapChain);

// =============================================================================
// System factory vtable hooks
// IDXGIFactory2 vtable layout (0-indexed):
//   [0-2]  IUnknown
//   [3-6]  IDXGIObject
//   [7-11] IDXGIFactory  (CreateSwapChain=10)
//   [12-13] IDXGIFactory1
//   [14]   IDXGIFactory2::IsWindowedStereoEnabled
//   [15]   IDXGIFactory2::CreateSwapChainForHwnd   <- slot 15
// =============================================================================

// Function pointer types for system-DXGI factory CSC/CSCFH (used as trampoline type)
using PFN_SysCSC_t   = HRESULT(STDMETHODCALLTYPE*)(IDXGIFactory*, IUnknown*, DXGI_SWAP_CHAIN_DESC*, IDXGISwapChain**);
using PFN_SysCSCFH_t = HRESULT(STDMETHODCALLTYPE*)(IDXGIFactory2*,
                                                   IUnknown*,
                                                   HWND,
                                                   const DXGI_SWAP_CHAIN_DESC1*,
                                                   const DXGI_SWAP_CHAIN_FULLSCREEN_DESC*,
                                                   IDXGIOutput*,
                                                   IDXGISwapChain1**);

// Inline hook state for factory CreateSwapChain (vt[10])
static BYTE* g_cscHookTarget                    = nullptr;
static int   g_cscHookSize                      = 0;
static BYTE  g_cscOrigBytes[kHookOrigBytesSize] = {};
static BYTE* g_cscTrampoline                    = nullptr;

// Inline hook state for factory CreateSwapChainForHwnd (vt[15])
static BYTE* g_cscfhHookTarget                    = nullptr;
static int   g_cscfhHookSize                      = 0;
static BYTE  g_cscfhOrigBytes[kHookOrigBytesSize] = {};
static BYTE* g_cscfhTrampoline                    = nullptr;

static bool g_factoryHooksInstalled = false;

// Shared re-entry guard: set true while our swap chain hook is on the call stack.
// Prevents infinite recursion when g_captureFactory->CSCFH internally calls the
// real DXGI function body which we have inline-hooked.
static thread_local bool s_inSwapChainIntercept = false;

// Forward declarations for x64 instruction decoder (defined later near InstallInlineHook_D3D11CreateDevice).
static int   FindPrologueBoundary(const BYTE* fn, int minBytes);
static void  FixupTrampolineRelBranches(BYTE* tramp, const BYTE* fn, int N);
static BYTE* AllocModuleThunk(SIZE_T bytes);
static bool  IsModuleThunkPointer(const void* p);

// Generic inline hook helper -- installs a 14-byte JMP [RIP+0] hook at fn[].
// Returns true on success.  origBytesOut receives the overwritten bytes.
// sizeOut receives the actual patch size (>= 14, aligned to instruction boundary).
// trampolineOut is an executable buffer:
//   [0..N-1]   original N bytes (relative branches fixed up)
//   [N..N+13]  JMP [RIP+0] + 8-byte address of fn+N
// trampolineOut is nullptr if the instruction decoder could not find a boundary.
static bool InstallInlineHookAt(BYTE* fn, void* hookFn, BYTE* origBytesOut, int& sizeOut, BYTE*& trampolineOut)
{
    int  N         = FindPrologueBoundary(fn, kHookBytes);
    bool decoderOk = (N >= kHookBytes);
    if (!decoderOk)
        N = kHookBytes;
    sizeOut = N;

    memcpy(origBytesOut, fn, N);

    if (decoderOk)
    {
        trampolineOut = AllocModuleThunk(N + 16);
        if (!trampolineOut)
        {
            trampolineOut = reinterpret_cast<BYTE*>(
                VirtualAlloc(nullptr, N + 16, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE));
        }
        if (!trampolineOut)
            return false;

        memcpy(trampolineOut, fn, N);
        FixupTrampolineRelBranches(trampolineOut, fn, N);

        trampolineOut[N + 0]                             = 0xFF;
        trampolineOut[N + 1]                             = 0x25;
        trampolineOut[N + 2]                             = 0x00;
        trampolineOut[N + 3]                             = 0x00;
        trampolineOut[N + 4]                             = 0x00;
        trampolineOut[N + 5]                             = 0x00;
        *reinterpret_cast<BYTE**>(trampolineOut + N + 6) = fn + N;
    }
    else
    {
        trampolineOut = nullptr;
        ProxyLog("[InstallInlineHookAt] WARNING: decoder failed at %p, no trampoline", (void*)fn);
    }

    DWORD old = 0;
    VirtualProtect(fn, N, PAGE_EXECUTE_READWRITE, &old);
    fn[0]                             = 0xFF;
    fn[1]                             = 0x25;
    fn[2]                             = 0x00;
    fn[3]                             = 0x00;
    fn[4]                             = 0x00;
    fn[5]                             = 0x00;
    *reinterpret_cast<void**>(fn + 6) = hookFn;
    for (int i = kHookBytes; i < N; ++i) fn[i] = kNopByte;
    VirtualProtect(fn, N, old, &old);
    FlushInstructionCache(GetCurrentProcess(), fn, N);
    return true;
}

static void RemoveInlineHookAt(BYTE* fn, const BYTE* origBytes, int size)
{
    if (!fn || size <= 0)
        return;
    DWORD old = 0;
    VirtualProtect(fn, size, PAGE_EXECUTE_READWRITE, &old);
    memcpy(fn, origBytes, size);
    VirtualProtect(fn, size, old, &old);
    FlushInstructionCache(GetCurrentProcess(), fn, size);
}

// Forward declarations for factory hook functions (defined after InstallFactoryInlineHooks).
static HRESULT STDMETHODCALLTYPE HookSysFactoryCSC(IDXGIFactory*, IUnknown*, DXGI_SWAP_CHAIN_DESC*, IDXGISwapChain**);
static HRESULT STDMETHODCALLTYPE HookSysFactoryCSCFH(IDXGIFactory2*,
                                                     IUnknown*,
                                                     HWND,
                                                     const DXGI_SWAP_CHAIN_DESC1*,
                                                     const DXGI_SWAP_CHAIN_FULLSCREEN_DESC*,
                                                     IDXGIOutput*,
                                                     IDXGISwapChain1**);
static void                      RemoveFactoryInlineHooks();

// Install inline hooks on the CSC/CSCFH function bodies in dxgi.dll.
// Reads function addresses from the factory vtable WITHOUT patching the vtable,
// avoiding VirtualProtect on the vtable DATA page (which triggers Denuvo checks).
// Instead we hook the function bodies in the CODE section, which is much less
// likely to be checksummed by Denuvo.
static void InstallFactoryInlineHooks(IDXGIFactory2* f)
{
    if (g_factoryHooksInstalled)
        return;

    void** vt      = *reinterpret_cast<void***>(f);
    BYTE*  cscFn   = reinterpret_cast<BYTE*>(vt[kFactoryCreateSwapChainVtIdx]);
    BYTE*  cscfhFn = reinterpret_cast<BYTE*>(vt[kFactoryCreateSwapChainForHwndVtIdx]);

    bool ok1 = InstallInlineHookAt(
        cscFn, reinterpret_cast<void*>(&HookSysFactoryCSC), g_cscOrigBytes, g_cscHookSize, g_cscTrampoline);
    g_cscHookTarget = ok1 ? cscFn : nullptr;

    bool ok2 = InstallInlineHookAt(
        cscfhFn, reinterpret_cast<void*>(&HookSysFactoryCSCFH), g_cscfhOrigBytes, g_cscfhHookSize, g_cscfhTrampoline);
    g_cscfhHookTarget = ok2 ? cscfhFn : nullptr;

    g_factoryHooksInstalled = (ok1 && ok2);
    ProxyLog("[dxgi_proxy] factory hooks installed: cscTramp=%p cscfhTramp=%p",
             (void*)g_cscTrampoline,
             (void*)g_cscfhTrampoline);
}

static void RemoveFactoryInlineHooks()
{
    if (!g_factoryHooksInstalled)
        return;
    RemoveInlineHookAt(g_cscHookTarget, g_cscOrigBytes, g_cscHookSize);
    RemoveInlineHookAt(g_cscfhHookTarget, g_cscfhOrigBytes, g_cscfhHookSize);
    if (g_cscTrampoline)
    {
        if (!IsModuleThunkPointer(g_cscTrampoline))
            VirtualFree(g_cscTrampoline, 0, MEM_RELEASE);
        g_cscTrampoline = nullptr;
    }
    if (g_cscfhTrampoline)
    {
        if (!IsModuleThunkPointer(g_cscfhTrampoline))
            VirtualFree(g_cscfhTrampoline, 0, MEM_RELEASE);
        g_cscfhTrampoline = nullptr;
    }
    g_cscHookTarget         = nullptr;
    g_cscfhHookTarget       = nullptr;
    g_factoryHooksInstalled = false;
}

static HRESULT STDMETHODCALLTYPE HookSysFactoryCSC(IDXGIFactory*         pThis,
                                                   IUnknown*             pDevice,
                                                   DXGI_SWAP_CHAIN_DESC* pDesc,
                                                   IDXGISwapChain**      ppSC)
{
    // Re-entrant call: bypass our hook and call through the trampoline.
    if (s_inSwapChainIntercept)
    {
        if (g_cscTrampoline)
            return reinterpret_cast<PFN_SysCSC_t>(g_cscTrampoline)(pThis, pDevice, pDesc, ppSC);
        return DXGI_ERROR_INVALID_CALL;
    }

    s_inSwapChainIntercept = true;
    HRESULT result         = DXGI_ERROR_INVALID_CALL;

    if (pDevice && pDesc && ppSC && !g_creatingDevice)
    {
        DXGI_SWAP_CHAIN_DESC1 desc1 = {};
        ConvertDesc(*pDesc, desc1);
        ComPtr<IDXGISwapChain1> sc1;
        IDXGIFactory2*          capFact =
            g_captureFactory.Get() ? g_captureFactory.Get() : reinterpret_cast<IDXGIFactory2*>(pThis);
        HRESULT hr = MakeProxySwapChain(pDevice, capFact, pDesc->OutputWindow, &desc1, nullptr, nullptr, pDesc, &sc1);
        if (SUCCEEDED(hr))
        {
            *ppSC  = sc1.Detach();
            result = S_OK;
        }
        else
        {
            ProxyLog("[HookSysFactoryCSC] MakeProxySwapChain 0x%08X -> fallback", (unsigned)hr);
            result = g_cscTrampoline ? reinterpret_cast<PFN_SysCSC_t>(g_cscTrampoline)(pThis, pDevice, pDesc, ppSC)
                                     : DXGI_ERROR_INVALID_CALL;
        }
    }
    else
    {
        result = g_cscTrampoline ? reinterpret_cast<PFN_SysCSC_t>(g_cscTrampoline)(pThis, pDevice, pDesc, ppSC)
                                 : DXGI_ERROR_INVALID_CALL;
    }

    s_inSwapChainIntercept = false;
    return result;
}

static HRESULT STDMETHODCALLTYPE HookSysFactoryCSCFH(IDXGIFactory2*                         pThis,
                                                     IUnknown*                              pDevice,
                                                     HWND                                   hWnd,
                                                     const DXGI_SWAP_CHAIN_DESC1*           pDesc,
                                                     const DXGI_SWAP_CHAIN_FULLSCREEN_DESC* pFsDesc,
                                                     IDXGIOutput*                           pRestrict,
                                                     IDXGISwapChain1**                      ppSC)
{
    // Re-entrant call (e.g. from MakeProxySwapChain calling g_captureFactory->CSCFH,
    // which internally calls m_real->vtable[15] = this hooked function body).
    // Call through the trampoline to reach the real DXGI implementation.
    if (s_inSwapChainIntercept)
    {
        if (g_cscfhTrampoline)
            return reinterpret_cast<PFN_SysCSCFH_t>(g_cscfhTrampoline)(
                pThis, pDevice, hWnd, pDesc, pFsDesc, pRestrict, ppSC);
        return DXGI_ERROR_INVALID_CALL;
    }

    s_inSwapChainIntercept = true;
    HRESULT result         = DXGI_ERROR_INVALID_CALL;

    if (pDevice && pDesc && ppSC && hWnd && !g_creatingDevice)
    {
        DXGI_SWAP_CHAIN_DESC origDesc = BuildLegacyDesc(hWnd, pDesc, pFsDesc);
        IDXGIFactory2*       capFact  = g_captureFactory.Get() ? g_captureFactory.Get() : pThis;
        HRESULT hr = MakeProxySwapChain(pDevice, capFact, hWnd, pDesc, pFsDesc, pRestrict, &origDesc, ppSC);
        if (SUCCEEDED(hr))
        {
            result = S_OK;
        }
        else
        {
            ProxyLog("[HookSysFactoryCSCFH] MakeProxySwapChain 0x%08X -> fallback", (unsigned)hr);
            result = g_cscfhTrampoline ? reinterpret_cast<PFN_SysCSCFH_t>(g_cscfhTrampoline)(
                                             pThis, pDevice, hWnd, pDesc, pFsDesc, pRestrict, ppSC)
                                       : DXGI_ERROR_INVALID_CALL;
        }
    }
    else
    {
        result = g_cscfhTrampoline ? reinterpret_cast<PFN_SysCSCFH_t>(g_cscfhTrampoline)(
                                         pThis, pDevice, hWnd, pDesc, pFsDesc, pRestrict, ppSC)
                                   : DXGI_ERROR_INVALID_CALL;
    }

    s_inSwapChainIntercept = false;
    return result;
}

// =============================================================================
// MakeProxySwapChain: create a D3D12 swap chain + ProxySwapChain wrapper
// =============================================================================

static HRESULT MakeProxySwapChain(IUnknown*                              pDevice,
                                  IDXGIFactory2*                         pRealFactory,
                                  HWND                                   hwnd,
                                  const DXGI_SWAP_CHAIN_DESC1*           pDesc1,
                                  const DXGI_SWAP_CHAIN_FULLSCREEN_DESC* pFsDesc,
                                  IDXGIOutput*                           pRestrictToOutput,
                                  const DXGI_SWAP_CHAIN_DESC*            pOriginalDesc,
                                  IDXGISwapChain1**                      ppSwapChain)
{
    // Resolve D3D11 device
    ComPtr<ID3D11Device> pD3D11Dev;
    if (FAILED(pDevice->QueryInterface(IID_PPV_ARGS(&pD3D11Dev))))
        return E_NOINTERFACE;

    std::shared_ptr<DeviceContext> ctx = LookupDeviceContext(pD3D11Dev.Get());
    if (!ctx)
        return E_FAIL;

    // Build D3D12-compatible swap chain desc (strip SRGB, force FLIP, >= 2 buffers)
    DXGI_SWAP_CHAIN_DESC1 d12Desc = *pDesc1;
    if (d12Desc.Format == DXGI_FORMAT_B8G8R8A8_UNORM_SRGB)
        d12Desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    else if (d12Desc.Format == DXGI_FORMAT_R8G8B8A8_UNORM_SRGB)
        d12Desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    if (d12Desc.SwapEffect != DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL && d12Desc.SwapEffect != DXGI_SWAP_EFFECT_FLIP_DISCARD)
        d12Desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    if (d12Desc.BufferCount < 2)
        d12Desc.BufferCount = 2;
    d12Desc.SampleDesc = { 1, 0 };
    d12Desc.Flags &= ~DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;

    // If Width/Height are 0 (DXGI "use window size" convention), fill in real dimensions from the
    // HWND client rect so gfxrecon captures actual values and replay can create a correctly-sized window.
    if ((d12Desc.Width == 0 || d12Desc.Height == 0) && hwnd)
    {
        RECT rc = {};
        if (GetClientRect(hwnd, &rc))
        {
            if (d12Desc.Width == 0)
                d12Desc.Width = static_cast<UINT>(rc.right - rc.left);
            if (d12Desc.Height == 0)
                d12Desc.Height = static_cast<UINT>(rc.bottom - rc.top);
            ProxyLog("[MakeProxySwapChain] auto-sized from HWND: %ux%u", d12Desc.Width, d12Desc.Height);
        }
    }

    // Create D3D12 swap chain via g_captureFactory so the gfxrecon capture layer records
    // the call.  The call internally reaches our inline-hooked CSCFH function body, but
    // s_inSwapChainIntercept is already true (set by the outer factory hook that called us),
    // so the re-entry guard in HookSysFactoryCSCFH routes it through the trampoline to the
    // real DXGI implementation -- no infinite recursion and no vtable patching required.
    ComPtr<IDXGISwapChain1> realSC;
    HRESULT                 hr              = E_FAIL;
    ProxySwapChain*         oldProxy        = g_activeProxy;
    auto                    ReleaseOldProxy = [&]() {
        if (oldProxy)
        {
            // Deactivate before releasing the internal ref: this releases the D3D12 swap
            // chain immediately so D3D12 does not see two concurrent swap chains on the
            // same command queue (which causes std::system_error after ~10 presents).
            oldProxy->Deactivate();
            oldProxy->Release();
            if (g_activeProxy == oldProxy)
                g_activeProxy = nullptr;
            oldProxy = nullptr;
        }
    };
    auto CreateRealSwapChain = [&]() -> HRESULT {
        realSC.Reset();
        if (g_captureFactory.Get())
        {
            return g_captureFactory->CreateSwapChainForHwnd(
                ctx->cmdQueue.Get(), hwnd, &d12Desc, pFsDesc, pRestrictToOutput, &realSC);
        }
        if (g_cscfhTrampoline)
        {
            // No capture factory: call real DXGI CSCFH via trampoline.
            return reinterpret_cast<PFN_SysCSCFH_t>(g_cscfhTrampoline)(
                pRealFactory, ctx->cmdQueue.Get(), hwnd, &d12Desc, pFsDesc, pRestrictToOutput, &realSC);
        }
        return DXGI_ERROR_INVALID_CALL;
    };

    // Deactivate and release the old proxy BEFORE creating the new D3D12 swap chain.
    // D3D12 does not support two concurrent swap chains on the same command queue:
    // keeping both alive causes a std::system_error on D3D12's background thread.
    ReleaseOldProxy();

    hr = CreateRealSwapChain();
    if (FAILED(hr))
    {
        ProxyLog("[MakeProxySwapChain] CreateSwapChainForHwnd hr=0x%08X", (unsigned)hr);
        return hr;
    }

    ComPtr<IDXGISwapChain3> realSC3;
    hr = realSC->QueryInterface(IID_PPV_ARGS(&realSC3));
    if (FAILED(hr))
    {
        ProxyLog("[MakeProxySwapChain] QI IDXGISwapChain3 hr=0x%08X", (unsigned)hr);
        return hr;
    }

    // Build legacy desc for GetDesc()
    DXGI_SWAP_CHAIN_DESC legacyDesc = pOriginalDesc ? *pOriginalDesc : BuildLegacyDesc(hwnd, pDesc1, pFsDesc);

    ProxySwapChain* proxy = new ProxySwapChain(realSC3.Get(),
                                               pD3D11Dev.Get(),
                                               ctx->d3d11Context.Get(),
                                               ctx->d3d11On12Device.Get(),
                                               ctx->cmdQueue.Get(),
                                               legacyDesc,
                                               d12Desc);

    // Hold an internal reference so the proxy survives the app's Release().
    g_activeProxy = proxy;
    proxy->AddRef(); // internal ref -- refcount is now 2
    ReleaseOldProxy();

    *ppSwapChain = static_cast<IDXGISwapChain1*>(proxy);
    ProxyLog("[MakeProxySwapChain] OK proxy=%p %ux%u fmt=%u bufs=%u",
             (void*)proxy,
             d12Desc.Width,
             d12Desc.Height,
             (unsigned)d12Desc.Format,
             d12Desc.BufferCount);

    return S_OK;
}

// =============================================================================
// IAT patching + D3D11CreateDevice hook
// =============================================================================

static HRESULT WINAPI HookD3D11CreateDevice(IDXGIAdapter*            pAdapter,
                                            D3D_DRIVER_TYPE          DriverType,
                                            HMODULE                  Software,
                                            UINT                     Flags,
                                            const D3D_FEATURE_LEVEL* pFeatureLevels,
                                            UINT                     FeatureLevels,
                                            UINT                     SDKVersion,
                                            ID3D11Device**           ppDevice,
                                            D3D_FEATURE_LEVEL*       pFeatureLevel,
                                            ID3D11DeviceContext**    ppImmediateContext);

// Patch D3D11CreateDevice in one module's IAT. Records each patched thunk.
static void PatchSingleModuleIAT(HMODULE hMod)
{
    if (!hMod)
        return;
    auto* pDos = reinterpret_cast<PIMAGE_DOS_HEADER>(hMod);
    if (pDos->e_magic != IMAGE_DOS_SIGNATURE)
        return;
    auto* pNT    = reinterpret_cast<PIMAGE_NT_HEADERS>(reinterpret_cast<BYTE*>(hMod) + pDos->e_lfanew);
    auto& impDir = pNT->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
    if (!impDir.VirtualAddress)
        return;

    auto* pDesc = reinterpret_cast<PIMAGE_IMPORT_DESCRIPTOR>(reinterpret_cast<BYTE*>(hMod) + impDir.VirtualAddress);

    for (; pDesc->Name; ++pDesc)
    {
        const char* dllName = reinterpret_cast<char*>(reinterpret_cast<BYTE*>(hMod) + pDesc->Name);
        if (_stricmp(dllName, "d3d11.dll") != 0)
            continue;

        // Some linkers set OriginalFirstThunk to 0; fall back to FirstThunk for names.
        DWORD origRVA = pDesc->OriginalFirstThunk ? pDesc->OriginalFirstThunk : pDesc->FirstThunk;
        auto* pOrig   = reinterpret_cast<PIMAGE_THUNK_DATA>(reinterpret_cast<BYTE*>(hMod) + origRVA);
        auto* pThunk  = reinterpret_cast<PIMAGE_THUNK_DATA>(reinterpret_cast<BYTE*>(hMod) + pDesc->FirstThunk);

        for (UINT i = 0; pOrig[i].u1.AddressOfData; ++i)
        {
            if (IMAGE_SNAP_BY_ORDINAL(pOrig[i].u1.Ordinal))
                continue;
            auto* pName =
                reinterpret_cast<PIMAGE_IMPORT_BY_NAME>(reinterpret_cast<BYTE*>(hMod) + pOrig[i].u1.AddressOfData);
            if (strcmp(reinterpret_cast<char*>(pName->Name), "D3D11CreateDevice") != 0)
                continue;

            ULONGLONG cur = pThunk[i].u1.Function;
            // Skip already-patched thunks
            if (cur == reinterpret_cast<ULONGLONG>(&HookD3D11CreateDevice))
                break;

            // First seen original -> save as the function to call through
            if (!g_iatOrig_D3D11Create)
                g_iatOrig_D3D11Create = reinterpret_cast<PFN_D3D11CreateDevice_t>(cur);

            DWORD old = 0;
            VirtualProtect(&pThunk[i].u1.Function, sizeof(ULONGLONG), PAGE_READWRITE, &old);
            g_patchedThunks.push_back({ &pThunk[i], cur });
            pThunk[i].u1.Function = reinterpret_cast<ULONGLONG>(&HookD3D11CreateDevice);
            VirtualProtect(&pThunk[i].u1.Function, sizeof(ULONGLONG), old, &old);
            break; // one entry per module
        }
        break; // found the d3d11.dll import descriptor
    }
}

static void PatchIAT_D3D11CreateDevice()
{
    // Snapshot all currently loaded modules and patch each one's IAT.
    HANDLE hSnap = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE, GetCurrentProcessId());
    if (hSnap != INVALID_HANDLE_VALUE)
    {
        MODULEENTRY32W me = {};
        me.dwSize         = sizeof(me);
        if (Module32FirstW(hSnap, &me))
        {
            do
            {
                // Don't patch d3d11.dll
                if (me.hModule != g_hD3D11Sys)
                    PatchSingleModuleIAT(me.hModule);
            } while (Module32NextW(hSnap, &me));
        }
        CloseHandle(hSnap);
    }
    else
    {
        // Fallback: patch main EXE only
        PatchSingleModuleIAT(GetModuleHandleW(nullptr));
    }

    g_iatPatched = !g_patchedThunks.empty();
    if (!g_iatPatched)
        ProxyLog("[dxgi_proxy] WARNING: D3D11CreateDevice not found in any module IAT");
}

// Forward declarations
static void RemoveInlineHook_D3D11CreateDevice();

// Restore IAT to original D3D11CreateDevice after we've created the D3D11On12 device.
// This prevents Denuvo's periodic IAT integrity check from detecting the hook.
static void RestoreIAT_D3D11CreateDevice()
{
    AcquireSRWLockExclusive(&g_iatLock);
    if (g_iatPatched)
    {
        SIZE_T n = g_patchedThunks.size();
        for (auto& pt : g_patchedThunks)
        {
            DWORD old = 0;
            VirtualProtect(&pt.thunk->u1.Function, sizeof(ULONGLONG), PAGE_READWRITE, &old);
            pt.thunk->u1.Function = pt.origAddr;
            VirtualProtect(&pt.thunk->u1.Function, sizeof(ULONGLONG), old, &old);
        }
        g_patchedThunks.clear();
        g_iatPatched = false;
    }
    ReleaseSRWLockExclusive(&g_iatLock);
    // Also remove the inline hook so Denuvo's code-integrity scan doesn't flag it.
    RemoveInlineHook_D3D11CreateDevice();
}

// =============================================================================
// Inline hook on D3D11CreateDevice inside d3d11.dll
// =============================================================================
// Patches the first N bytes of D3D11CreateDevice in d3d11.dll with an
// absolute indirect JMP to HookD3D11CreateDevice.  N is chosen as the
// smallest instruction boundary >= 14 (the minimum hook size) using a
// minimal x64 prologue instruction length decoder.
//
// Hook bytes written (14 bytes, padded to N with NOPs):
//   FF 25 00 00 00 00          ; JMP QWORD PTR [RIP+0]
//   <8-byte hook address>      ; absolute target
//   [NOP * (N-14)]             ; padding if boundary > 14
//
// Trampoline layout (PAGE_EXECUTE_READWRITE):
//   [0..N-1]   : copy of original N bytes (relative branches fixed up)
//   [N..N+5]   : FF 25 00 00 00 00  (JMP [RIP+0])
//   [N+6..N+13]: original function + N (return to after hook)
//
// If the decoder cannot determine the boundary (unknown opcodes), the hook is
// still installed but g_inlineHookTrampoline is set to nullptr.  Re-entrant
// calls then use a "temporarily unhook / call real / rehook" path instead.

static BYTE* g_inlineHookTrampoline                    = nullptr;
static BYTE* g_inlineHookTarget                        = nullptr; // &D3D11CreateDevice in d3d11.dll
static int   g_inlineHookSize                          = 0;       // number of bytes patched (N)
static BYTE  g_inlineHookOrigBytes[kHookOrigBytesSize] = {};      // saved original bytes (always valid)

// ---------------------------------------------------------------------------
// Minimal x64 instruction length decoder for common function-prologue patterns.
// Handles all opcodes likely to appear in the first 32 bytes of a Windows
// system-DLL export, including ENDBR64 (CET), multi-byte NOPs, and relative
// branches.  Returns the byte length of the instruction at p[], or -1 if the
// opcode is not recognised.
// ---------------------------------------------------------------------------
static int PrologueInstrLen(const BYTE* p)
{
    int i = 0;

    // ENDBR64: F3 0F 1E FA  (Intel CET, 4 bytes; treat as atomic sequence)
    if (p[0] == 0xF3 && p[1] == 0x0F && p[2] == 0x1E && p[3] == 0xFA)
        return 4;

    // Consume REX prefix (40-4F)
    if ((p[i] & 0xF0) == 0x40)
        i++;
    BYTE op = p[i++];

    // Two-byte escape (0F xx)
    if (op == 0x0F)
    {
        BYTE op2 = p[i++];
        if (op2 == 0x1F)
        {
            // Multi-byte NOP: 0F 1F /0  (various forms: 2-9 bytes)
            BYTE m   = p[i++];
            int  mod = m >> 6, rm = m & 7;
            if (mod != 3)
            {
                if (rm == 4)
                    i++; // SIB
                if (mod == 1)
                    i++; // disp8
                else if (mod == 2)
                    i += 4; // disp32
                else if (rm == 5)
                    i += 4; // RIP+disp32
            }
            return i;
        }
        return -1; // other 0F opcodes: not handled
    }

    // ModRM decoder (captures SIB and displacement)
    auto skipModRM = [&]() {
        BYTE m   = p[i++];
        int  mod = m >> 6, rm = m & 7;
        if (mod == 3)
            return;
        if (rm == 4)
            i++; // SIB byte
        if (mod == 1)
            i++; // disp8
        else if (mod == 2)
            i += 4; // disp32
        else if (rm == 5)
            i += 4; // RIP+disp32 (mod=0, rm=5)
    };

    switch (op)
    {
        // PUSH / POP r64 (1 byte each)
        case 0x50:
        case 0x51:
        case 0x52:
        case 0x53:
        case 0x54:
        case 0x55:
        case 0x56:
        case 0x57:
        case 0x58:
        case 0x59:
        case 0x5A:
        case 0x5B:
        case 0x5C:
        case 0x5D:
        case 0x5E:
        case 0x5F:
        case 0x90: // NOP (1 byte)
        case 0xC3:
        case 0xCC: // RET / INT3
            break;

        case 0x89:
        case 0x8B:
        case 0x8D: // MOV r/m64, r  /  MOV r, r/m  /  LEA
            skipModRM();
            break;
        case 0x83: // OP r/m64, imm8
            skipModRM();
            i++;
            break;
        case 0x81:
        case 0xC7: // OP r/m64, imm32  /  MOV r/m64, imm32
            skipModRM();
            i += 4;
            break;
        case 0xFF: // INC/DEC/CALL/JMP [r/m]
            skipModRM();
            break;

        case 0xE8:
        case 0xE9: // CALL rel32 / JMP rel32
            i += 4;
            break;
        case 0xEB: // JMP rel8
            i++;
            break;

        default:
            return -1; // unrecognised -- caller must treat as failure
    }
    return i;
}

// Find the first instruction boundary at offset >= minBytes.
// Returns -1 if any instruction in the scan window is not recognised.
static int FindPrologueBoundary(const BYTE* fn, int minBytes)
{
    int pos = 0;
    while (pos < minBytes)
    {
        int len = PrologueInstrLen(fn + pos);
        if (len <= 0)
            return -1;
        pos += len;
    }
    return pos;
}

// Fix up relative branch instructions that were copied from fn[] into the
// trampoline at a different address.  Must be called BEFORE patching fn[].
static void FixupTrampolineRelBranches(BYTE* tramp, const BYTE* fn, int N)
{
    int pos = 0;
    while (pos < N)
    {
        const BYTE* src = fn + pos;
        BYTE*       dst = tramp + pos;

        // ENDBR64 -- no fixup needed
        if (pos + 4 <= N && src[0] == 0xF3 && src[1] == 0x0F && src[2] == 0x1E && src[3] == 0xFA)
        {
            pos += 4;
            continue;
        }

        int  rexOff = ((src[0] & 0xF0) == 0x40) ? 1 : 0;
        BYTE op     = src[rexOff];

        if (op == 0xE9 || op == 0xE8)
        {
            // JMP rel32 / CALL rel32: fix up the 32-bit offset
            int32_t   origRel                             = *reinterpret_cast<const int32_t*>(src + rexOff + 1);
            uintptr_t target                              = (uintptr_t)(src + rexOff + 5) + (intptr_t)origRel;
            int32_t   newRel                              = (int32_t)((intptr_t)target - (intptr_t)(dst + rexOff + 5));
            *reinterpret_cast<int32_t*>(dst + rexOff + 1) = newRel;
        }
        // EB (short JMP rel8): expanding to E9 rel32 would require re-laying out
        // the trampoline, which is too complex.  EB is never expected in a real
        // function prologue; if it does appear the decoder falls back to the
        // no-trampoline path, so we never reach this fixup.
        else if (op == 0xFF && pos + rexOff + 6 <= N)
        {
            // JMP/CALL [RIP+rel32]: fix up the 32-bit displacement.
            BYTE modrm = src[rexOff + 1];
            if ((modrm & 0xC7) == 0x05)
            { // mod=0, rm=5 -> RIP-relative
                int32_t   origDisp = *reinterpret_cast<const int32_t*>(src + rexOff + 2);
                uintptr_t ptrAddr  = (uintptr_t)(src + rexOff + 6) + origDisp;
                int32_t   newDisp  = (int32_t)((intptr_t)ptrAddr - (intptr_t)(dst + rexOff + 6));
                *reinterpret_cast<int32_t*>(dst + rexOff + 2) = newDisp;
            }
        }

        int len = PrologueInstrLen(src);
        if (len <= 0)
            break;
        pos += len;
    }
}

static void InstallInlineHook_D3D11CreateDevice()
{
    if (!g_hD3D11Sys || g_inlineHookTarget)
        return;

    BYTE* fn = reinterpret_cast<BYTE*>(GetProcAddress(g_hD3D11Sys, "D3D11CreateDevice"));
    if (!fn)
        return;

    int  N         = FindPrologueBoundary(fn, kHookBytes);
    bool decoderOk = (N >= kHookBytes);
    if (!decoderOk)
    {
        // Decoder hit an unrecognised opcode -- fall back to minimum patch size.
        // The trampoline is NOT built; re-entrant calls will use the
        // unhook/call/rehook path instead.
        N = kHookBytes;
        ProxyLog("[dxgi_proxy] WARNING: instruction boundary scan failed, "
                 "installing hook at N=%d without trampoline",
                 N);
    }
    g_inlineHookSize = N;

    // Always save original bytes so RemoveInlineHook can restore them even
    // when no trampoline was built.
    memcpy(g_inlineHookOrigBytes, fn, N);

    if (decoderOk)
    {
        // Build executable trampoline: N original bytes + 14-byte JMP-back
        g_inlineHookTrampoline = AllocModuleThunk(N + 16);
        if (!g_inlineHookTrampoline)
        {
            g_inlineHookTrampoline = reinterpret_cast<BYTE*>(
                VirtualAlloc(nullptr, N + 16, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE));
        }
        if (!g_inlineHookTrampoline)
            return;

        memcpy(g_inlineHookTrampoline, fn, N);

        // Fix up any relative branches in the copied bytes so they still
        // target the original destinations when executed from the trampoline.
        FixupTrampolineRelBranches(g_inlineHookTrampoline, fn, N);

        // Append JMP [RIP+0] -> fn+N  (jump back to continue original function)
        g_inlineHookTrampoline[N + 0]                             = 0xFF;
        g_inlineHookTrampoline[N + 1]                             = 0x25;
        g_inlineHookTrampoline[N + 2]                             = 0x00;
        g_inlineHookTrampoline[N + 3]                             = 0x00;
        g_inlineHookTrampoline[N + 4]                             = 0x00;
        g_inlineHookTrampoline[N + 5]                             = 0x00;
        *reinterpret_cast<BYTE**>(g_inlineHookTrampoline + N + 6) = fn + N;

        // Point g_iatOrig_D3D11Create at the trampoline so re-entrant calls
        // go through the saved prologue and then to fn+N.
        g_iatOrig_D3D11Create = reinterpret_cast<PFN_D3D11CreateDevice_t>(g_inlineHookTrampoline);
    }
    // else: g_iatOrig_D3D11Create stays nullptr; re-entrant path uses
    //       unhook/call/rehook fallback (see HookD3D11CreateDevice).

    // Patch D3D11CreateDevice in-place: 14-byte hook + NOP padding to N
    DWORD old = 0;
    VirtualProtect(fn, N, PAGE_EXECUTE_READWRITE, &old);

    fn[0]                             = 0xFF;
    fn[1]                             = 0x25;
    fn[2]                             = 0x00;
    fn[3]                             = 0x00;
    fn[4]                             = 0x00;
    fn[5]                             = 0x00;
    *reinterpret_cast<void**>(fn + 6) = reinterpret_cast<void*>(&HookD3D11CreateDevice);
    for (int i = kHookBytes; i < N; ++i) fn[i] = kNopByte;

    VirtualProtect(fn, N, old, &old);
    FlushInstructionCache(GetCurrentProcess(), fn, N);

    g_inlineHookTarget = fn;
}

// Remove inline hook (called only in Denuvo mode, together with IAT restore).
static void RemoveInlineHook_D3D11CreateDevice()
{
    if (!g_inlineHookTarget || g_inlineHookSize <= 0)
        return;
    DWORD old = 0;
    VirtualProtect(g_inlineHookTarget, g_inlineHookSize, PAGE_EXECUTE_READWRITE, &old);
    // Restore from g_inlineHookOrigBytes (always valid; trampoline may be null)
    memcpy(g_inlineHookTarget, g_inlineHookOrigBytes, g_inlineHookSize);
    VirtualProtect(g_inlineHookTarget, g_inlineHookSize, old, &old);
    FlushInstructionCache(GetCurrentProcess(), g_inlineHookTarget, g_inlineHookSize);
    g_inlineHookTarget = nullptr;
    g_inlineHookSize   = 0;
}

// Call the real D3D11CreateDevice bypassing our hook.  Used for re-entrant
// calls (from inside D3D11On12CreateDevice) when the trampoline is available,
// or via unhook/call/rehook when it is not.
static HRESULT CallRealD3D11CreateDevice(IDXGIAdapter*            pAdapter,
                                         D3D_DRIVER_TYPE          DriverType,
                                         HMODULE                  Software,
                                         UINT                     Flags,
                                         const D3D_FEATURE_LEVEL* pFeatureLevels,
                                         UINT                     FeatureLevels,
                                         UINT                     SDKVersion,
                                         ID3D11Device**           ppDevice,
                                         D3D_FEATURE_LEVEL*       pFeatureLevel,
                                         ID3D11DeviceContext**    ppImmediateContext)
{
    if (g_iatOrig_D3D11Create)
    {
        // Fast path: trampoline available
        return g_iatOrig_D3D11Create(pAdapter,
                                     DriverType,
                                     Software,
                                     Flags,
                                     pFeatureLevels,
                                     FeatureLevels,
                                     SDKVersion,
                                     ppDevice,
                                     pFeatureLevel,
                                     ppImmediateContext);
    }

    // Slow path (decoder failure, no trampoline): temporarily remove the
    // inline hook, call the real function, then reinstall the hook.
    // NOTE: another thread could call D3D11CreateDevice in the small window
    // between unhook and rehook and go unintercepted, but device creation is
    // effectively always single-threaded in practice.
    RemoveInlineHook_D3D11CreateDevice();

    PFN_D3D11CreateDevice_t realFn =
        reinterpret_cast<PFN_D3D11CreateDevice_t>(GetProcAddress(g_hD3D11Sys, "D3D11CreateDevice"));
    HRESULT hr = E_FAIL;
    if (realFn)
        hr = realFn(pAdapter,
                    DriverType,
                    Software,
                    Flags,
                    pFeatureLevels,
                    FeatureLevels,
                    SDKVersion,
                    ppDevice,
                    pFeatureLevel,
                    ppImmediateContext);

    InstallInlineHook_D3D11CreateDevice();
    return hr;
}

static void LazyInitD3D12()
{
    AcquireSRWLockExclusive(&g_iatLock);
    bool needInit = (!g_hD3D12 && !g_inlineHookTarget);
    ReleaseSRWLockExclusive(&g_iatLock);
    if (!needInit)
        return;

    AcquireSRWLockExclusive(&g_iatLock);
    if (!g_hD3D12)
    {
        g_hD3D12 = LoadLibraryW(L"d3d12.dll");
        if (g_hD3D12)
            g_D3D12CreateDevice =
                reinterpret_cast<PFN_D3D12CreateDevice_t>(GetProcAddress(g_hD3D12, "D3D12CreateDevice"));
    }
    if (!g_hD3D11Sys)
    {
        g_hD3D11Sys = LoadLibraryExW(L"d3d11.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
        if (g_hD3D11Sys)
        {
            g_D3D11On12Create =
                reinterpret_cast<PFN_D3D11On12CreateDevice_t>(GetProcAddress(g_hD3D11Sys, "D3D11On12CreateDevice"));
        }
    }
    ReleaseSRWLockExclusive(&g_iatLock);

    // Inline hook: patches the actual function bytes in d3d11.dll so ALL callers
    // (IAT-resolved, GetProcAddress-cached, direct) are intercepted.
    // Also sets g_iatOrig_D3D11Create = trampoline for calling the real function.
    InstallInlineHook_D3D11CreateDevice();

    // Also patch all module IATs (belt-and-suspenders for statically-linked callers).
    if (!g_iatPatched)
        PatchIAT_D3D11CreateDevice();
}

// =============================================================================
// COM Wrapper for D3D11On12 -- clean-memory facade to bypass Denuvo memory scan
// =============================================================================
// Denuvo scans the D3D11 device/context COM objects for D3D12 pointers stored
// internally by D3D11On12.  The wrappers below present "clean" memory to any
// scanner (vtable ptr + refCount only, zero D3D12 pointers), while forwarding
// every virtual call to the real D3D11On12 objects stored in proxy-DLL globals.
//
// Denuvo also checks that device->vtable points WITHIN d3d11.dll's address
// range (module check). To pass this, the vtable arrays are written into a
// zero-filled code cave found by scanning d3d11.dll's .rdata section.
//
// Stub layout (24 bytes each):
//   MOV RCX, <realThis>   ; 10 bytes -- replace wrapper ptr with real object ptr
//   JMP [RIP+0]           ; 6 bytes  -- indirect abs jump
//   <8-byte target addr>  ; 8 bytes  -- vtable[i] of realThis

static ID3D11Device*        g_on12Dev       = nullptr; // real D3D11On12 device
static ID3D11DeviceContext* g_on12Ctx       = nullptr; // real D3D11On12 context
static ID3D11On12Device*    g_on12Interface = nullptr; // for VBB AcquireWrapped*
// "Clean" real D3D11 device returned to the game as ppDevice.
// Denuvo scans the COM object at the returned pointer -- a real D3D11 device
// has no D3D12 pointers in its memory, so Denuvo cannot detect D3D11On12.
// We replace its vtable pointer with our cave vtable so all virtual calls
// are intercepted and forwarded to g_on12Dev/g_on12Ctx for actual rendering.
static ID3D11Device*        g_cleanD3D11Dev = nullptr;
static ID3D11DeviceContext* g_cleanD3D11Ctx = nullptr;

// Original vtable function pointers saved before vtable hooking.
// Used by AddRef/Release/QI special handlers to call through the real COM impl.
typedef HRESULT(STDMETHODCALLTYPE* PFN_QI)(IUnknown*, REFIID, void**);
typedef ULONG(STDMETHODCALLTYPE* PFN_AddRef)(IUnknown*);
typedef ULONG(STDMETHODCALLTYPE* PFN_Release)(IUnknown*);
static PFN_QI      g_cleanDev_origQI      = nullptr;
static PFN_AddRef  g_cleanDev_origAddRef  = nullptr;
static PFN_Release g_cleanDev_origRelease = nullptr;
static PFN_QI      g_cleanCtx_origQI      = nullptr;
static PFN_AddRef  g_cleanCtx_origAddRef  = nullptr;
static PFN_Release g_cleanCtx_origRelease = nullptr;

// WrapperObj: heap-allocated (MEM_PRIVATE) to mimic real COM object allocation type.
// The vtable is stored in a d3d11.dll code cave so the vtable POINTER passes module checks.
struct WrapperObj
{
    void** vtable;
    LONG   refCount;
};
static WrapperObj* g_wrapDev = nullptr;
static WrapperObj* g_wrapCtx = nullptr;

// Stub pool -- forwarding stubs for vtable dispatch.
// Prefer d3d11.dll cave so stub targets stay inside a known module.  If d3d11.dll
// does not have enough executable padding, fall back to a dedicated executable
// section in this proxy DLL before finally using anonymous VirtualAlloc memory.
#ifdef _MSC_VER
#pragma section(".gxstub", execute, read)
__declspec(allocate(".gxstub")) static BYTE g_moduleStubPool[kStubPoolSize] = {};
#endif
static BYTE*  g_stubs         = nullptr;
static SIZE_T g_stubsUsed     = 0;
static bool   g_stubsFromCave = false; // true if g_stubs points into the cave
static bool   g_stubsNeedProt = false;
static SIZE_T g_stubsSize     = 0;
static DWORD  g_stubsOldProt  = 0;

// Code cave pool -- space found inside d3d11.dll's .data loader-pad
static BYTE*  g_cave     = nullptr;
static SIZE_T g_caveSize = 0;
static SIZE_T g_caveUsed = 0;

#ifdef _MSC_VER
#pragma section(".gxthnk", execute, read)
__declspec(allocate(".gxthnk")) static BYTE g_moduleThunkPool[kThunkPoolSize] = {};
#endif
static SIZE_T g_moduleThunkUsed     = 0;
static bool   g_moduleThunkWritable = false;
static BYTE*  AllocModuleThunk(SIZE_T bytes);
static bool   IsModuleThunkPointer(const void* p);

// ---------------------------------------------------------------------------
// PE section helpers (used by FindD3D11Cave / FindD3D11ExecCave)
// ---------------------------------------------------------------------------

// Copy the 8-byte (non-null-terminated) PE section name into a C string buffer.
static void GetSectionName(const IMAGE_SECTION_HEADER& sec, char (&buf)[kImageSectionNameLen + 1])
{
    memcpy(buf, sec.Name, kImageSectionNameLen);
    buf[kImageSectionNameLen] = '\0';
}

// Align a pointer up to an 8-byte boundary.
static BYTE* AlignUp8(BYTE* ptr)
{
    return reinterpret_cast<BYTE*>((reinterpret_cast<uintptr_t>(ptr) + 7) & ~uintptr_t(7));
}

// Scand3d11.dll for 'needed' consecutive zero bytes that are safe to overwrite.
// Prefers loader-zero-padding in EXECUTABLE sections (.text) since those pages are
// already PAGE_EXECUTE_READ -- no suspicious page-permission change needed.
// Falls back to loader-pad in non-exec sections (requires making the page executable).
static BYTE* FindD3D11Cave(SIZE_T needed)
{
    HMODULE hD3D11 = GetModuleHandleA("d3d11.dll");
    if (!hD3D11)
    {
        ProxyLog("[FindD3D11Cave] d3d11.dll not loaded");
        return nullptr;
    }

    BYTE* base = reinterpret_cast<BYTE*>(hD3D11);
    auto* dos  = reinterpret_cast<IMAGE_DOS_HEADER*>(base);
    if (dos->e_magic != IMAGE_DOS_SIGNATURE)
        return nullptr;
    auto* nt  = reinterpret_cast<IMAGE_NT_HEADERS64*>(base + dos->e_lfanew);
    auto* sec = IMAGE_FIRST_SECTION(nt);

    // Pass 1: loader-pad in EXECUTABLE sections (e.g. .text tail padding).
    // These bytes are in already-executable pages so LockCave PAGE_EXECUTE_READ
    // matches the original protection -- no suspicious page-perm change.
    for (WORD i = 0; i < nt->FileHeader.NumberOfSections; i++)
    {
        if (!(sec[i].Characteristics & IMAGE_SCN_MEM_EXECUTE))
            continue; // exec only
        if (sec[i].Misc.VirtualSize <= sec[i].SizeOfRawData)
            continue;

        DWORD padSize = sec[i].Misc.VirtualSize - sec[i].SizeOfRawData;
        if (padSize < needed)
            continue;

        char name[kImageSectionNameLen + 1] = {};
        GetSectionName(sec[i], name);

        BYTE* ptr       = base + sec[i].VirtualAddress + sec[i].SizeOfRawData;
        BYTE* aligned   = AlignUp8(ptr);
        DWORD available = padSize - (DWORD)(aligned - ptr);
        if (available >= needed)
            return aligned;
    }

    // Pass 2: loader-pad in non-exec sections (requires executable page promotion).
    for (WORD i = 0; i < nt->FileHeader.NumberOfSections; i++)
    {
        if (sec[i].Characteristics & IMAGE_SCN_MEM_EXECUTE)
            continue;
        if (sec[i].Misc.VirtualSize <= sec[i].SizeOfRawData)
            continue;

        DWORD padSize = sec[i].Misc.VirtualSize - sec[i].SizeOfRawData;
        if (padSize < needed)
            continue;

        char name[kImageSectionNameLen + 1] = {};
        GetSectionName(sec[i], name);

        BYTE* ptr       = base + sec[i].VirtualAddress + sec[i].SizeOfRawData;
        BYTE* aligned   = AlignUp8(ptr);
        DWORD available = padSize - (DWORD)(aligned - ptr);
        if (available >= needed)
            return aligned;
    }

    // Pass 3: scan non-exec sections for zero runs (less safe, log only best run)
    SIZE_T bestRun     = 0;
    char   bestSec[16] = {};
    BYTE*  bestAddr    = nullptr;
    for (WORD i = 0; i < nt->FileHeader.NumberOfSections; i++)
    {
        if (sec[i].Characteristics & IMAGE_SCN_MEM_EXECUTE)
            continue;
        if (sec[i].Misc.VirtualSize < needed)
            continue;
        char name[kImageSectionNameLen + 1] = {};
        GetSectionName(sec[i], name);
        BYTE*  start = base + sec[i].VirtualAddress;
        DWORD  vsize = sec[i].Misc.VirtualSize;
        SIZE_T run   = 0;
        BYTE*  runS  = nullptr;
        for (DWORD j = 0; j < vsize; j++)
        {
            if (start[j] == 0)
            {
                if (run == 0)
                    runS = start + j;
                run++;
            }
            else
            {
                run  = 0;
                runS = nullptr;
            }
            if (run > bestRun)
            {
                bestRun = run;
                strcpy_s(bestSec, name);
                bestAddr = runS;
            }
        }
    }
    ProxyLog("[FindD3D11Cave] NO safe cave found (need=%zu); best zero-run=%zu in %s at %p",
             needed,
             bestRun,
             bestSec,
             (void*)bestAddr);
    return nullptr;
}

// Scan executable sections of d3d11.dll for a zero-filled range that can host
// forwarding stubs.  Unlike the vtable cave, stubs must end up in executable
// memory, so we only search executable sections here.
static BYTE* FindD3D11ExecCave(SIZE_T needed)
{
    HMODULE hD3D11 = GetModuleHandleA("d3d11.dll");
    if (!hD3D11)
        return nullptr;

    BYTE* base = reinterpret_cast<BYTE*>(hD3D11);
    auto* dos  = reinterpret_cast<IMAGE_DOS_HEADER*>(base);
    if (dos->e_magic != IMAGE_DOS_SIGNATURE)
        return nullptr;
    auto* nt  = reinterpret_cast<IMAGE_NT_HEADERS64*>(base + dos->e_lfanew);
    auto* sec = IMAGE_FIRST_SECTION(nt);

    SIZE_T bestRun     = 0;
    BYTE*  bestAddr    = nullptr;
    char   bestSec[16] = {};

    for (WORD i = 0; i < nt->FileHeader.NumberOfSections; i++)
    {
        if (!(sec[i].Characteristics & IMAGE_SCN_MEM_EXECUTE))
            continue;

        char name[kImageSectionNameLen + 1] = {};
        GetSectionName(sec[i], name);
        BYTE*  start = base + sec[i].VirtualAddress;
        DWORD  vsize = sec[i].Misc.VirtualSize;
        SIZE_T run   = 0;
        BYTE*  runS  = nullptr;

        for (DWORD j = 0; j < vsize; j++)
        {
            if (start[j] == 0)
            {
                if (run == 0)
                    runS = start + j;
                run++;
                if (run >= needed)
                {
                    BYTE* aligned = AlignUp8(runS);
                    if (aligned + needed <= start + j + 1)
                    {
                        return aligned;
                    }
                }
            }
            else
            {
                if (run > bestRun)
                {
                    bestRun = run;
                    strcpy_s(bestSec, name);
                    bestAddr = runS;
                }
                run  = 0;
                runS = nullptr;
            }
        }
        if (run > bestRun)
        {
            bestRun = run;
            strcpy_s(bestSec, name);
            bestAddr = runS;
        }
    }

    ProxyLog("[FindD3D11ExecCave] NO exec cave found (need=%zu); best zero-run=%zu in %s at %p",
             needed,
             bestRun,
             bestSec,
             (void*)bestAddr);
    return nullptr;
}

static void InitStubPool()
{
    // Called only when d3d11.dll doesn't provide enough executable padding.
    if (!g_stubs)
    {
        DWORD oldProt = 0;
#ifdef _MSC_VER
        if (VirtualProtect(g_moduleStubPool, sizeof(g_moduleStubPool), PAGE_EXECUTE_READWRITE, &oldProt))
        {
            g_stubs         = g_moduleStubPool;
            g_stubsSize     = sizeof(g_moduleStubPool);
            g_stubsFromCave = false;
            g_stubsNeedProt = true;
            g_stubsOldProt  = oldProt;
        }
#endif
        if (!g_stubs)
        {
            g_stubs = reinterpret_cast<BYTE*>(
                VirtualAlloc(nullptr, kStubPoolSize, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE));
            if (g_stubs)
            {
                g_stubsSize     = kStubPoolSize;
                g_stubsFromCave = false;
                g_stubsNeedProt = false;
            }
        }
    }
}

// Unlink a DLL from the PEB module lists so module-enumeration APIs (CreateToolhelp32Snapshot,
// EnumProcessModules, etc.) cannot see it.  The DLL remains loaded and functional.
// Used to hide d3d11on12.dll from Denuvo's scheduled integrity scan.
static void UnlinkDllFromPEB(HMODULE hModule)
{
    if (!hModule)
        return;

    // winternl.h's PEB_LDR_DATA hides InLoadOrderModuleList behind Reserved fields.
    // Use the full known layout of PEB_LDR_DATA (x64 Windows 10+):
    //   offset  0: ULONG  Length
    //   offset  4: BOOL   Initialized
    //   offset  8: PVOID  SsHandle
    //   offset 16: LIST_ENTRY InLoadOrderModuleList         (16 bytes)
    //   offset 32: LIST_ENTRY InMemoryOrderModuleList       (16 bytes)
    //   offset 48: LIST_ENTRY InInitializationOrderModuleList (16 bytes)
    struct MY_LDR_DATA
    {
        ULONG      Length;
        BOOL       Initialized;
        PVOID      SsHandle;
        LIST_ENTRY InLoadOrderModuleList;
        LIST_ENTRY InMemoryOrderModuleList;
        LIST_ENTRY InInitializationOrderModuleList;
    };

    PPEB pPEB = NtCurrentTeb()->ProcessEnvironmentBlock;
    if (!pPEB || !pPEB->Ldr)
        return;

    auto* ldr = reinterpret_cast<MY_LDR_DATA*>(pPEB->Ldr);

    PLIST_ENTRY pHead  = &ldr->InLoadOrderModuleList;
    PLIST_ENTRY pEntry = pHead->Flink;

    // Byte offsets within LDR_DATA_TABLE_ENTRY (x64, Windows 10+):
    //   InLoadOrderLinks        = 0  (LIST_ENTRY, 16 bytes)
    //   InMemoryOrderLinks      = 16 (LIST_ENTRY, 16 bytes)
    //   InInitializationOrderLinks = 32 (LIST_ENTRY, 16 bytes)
    //   DllBase                 = 48 (PVOID, 8 bytes)
    static const int kLdrInMemoryOrderOffset = 16;
    static const int kLdrInInitOrderOffset   = 32;
    static const int kLdrDllBaseOffset       = 48;

    while (pEntry != pHead)
    {
        BYTE*       raw     = reinterpret_cast<BYTE*>(pEntry);
        PVOID       dllBase = *reinterpret_cast<PVOID*>(raw + kLdrDllBaseOffset);
        PLIST_ENTRY pNext   = pEntry->Flink;

        if (dllBase == static_cast<PVOID>(hModule))
        {
            auto Unlink = [](PLIST_ENTRY e) {
                e->Blink->Flink = e->Flink;
                e->Flink->Blink = e->Blink;
                e->Flink = e->Blink = e;
            };
            auto* memOrderEntry  = reinterpret_cast<PLIST_ENTRY>(raw + kLdrInMemoryOrderOffset);
            auto* initOrderEntry = reinterpret_cast<PLIST_ENTRY>(raw + kLdrInInitOrderOffset);
            Unlink(pEntry);         // InLoadOrderModuleList
            Unlink(memOrderEntry);  // InMemoryOrderModuleList
            Unlink(initOrderEntry); // InInitializationOrderModuleList
            return;
        }
        pEntry = pNext;
    }
    ProxyLog("[UnlinkDllFromPEB] d3d11on12.dll not found in PEB list (already unlinked?)");
}

// Allocate 'bytes' from the d3d11.dll cave (must be preceded by UnlockCave/LockCave).
static void* CaveAlloc(SIZE_T bytes)
{
    bytes = (bytes + 7) & ~SIZE_T(7); // align to 8 bytes
    if (!g_cave || g_caveUsed + bytes > g_caveSize)
        return nullptr;
    void* p = g_cave + g_caveUsed;
    g_caveUsed += bytes;
    return p;
}

static DWORD g_caveOldProt = 0;
static BYTE* AllocModuleThunk(SIZE_T bytes)
{
#ifdef _MSC_VER
    bytes = (bytes + 15) & ~SIZE_T(15);
    if (g_moduleThunkUsed + bytes > sizeof(g_moduleThunkPool))
        return nullptr;

    if (!g_moduleThunkWritable)
    {
        DWORD old = 0;
        if (!VirtualProtect(g_moduleThunkPool, sizeof(g_moduleThunkPool), PAGE_EXECUTE_READWRITE, &old))
            return nullptr;
        g_moduleThunkWritable = true;
    }

    BYTE* p = g_moduleThunkPool + g_moduleThunkUsed;
    g_moduleThunkUsed += bytes;
    return p;
#else
    (void)bytes;
    return nullptr;
#endif
}

static bool IsModuleThunkPointer(const void* p)
{
#ifdef _MSC_VER
    auto addr  = reinterpret_cast<uintptr_t>(p);
    auto start = reinterpret_cast<uintptr_t>(g_moduleThunkPool);
    auto end   = start + sizeof(g_moduleThunkPool);
    return (addr >= start) && (addr < end);
#else
    (void)p;
    return false;
#endif
}

static bool IsExecutableProtection(DWORD prot)
{
    prot &= 0xFF;
    return prot == PAGE_EXECUTE || prot == PAGE_EXECUTE_READ || prot == PAGE_EXECUTE_READWRITE ||
           prot == PAGE_EXECUTE_WRITECOPY;
}

static void UnlockStubPool()
{
    if (g_stubs && g_stubsNeedProt)
        VirtualProtect(g_stubs, g_stubsSize, PAGE_EXECUTE_READWRITE, &g_stubsOldProt);
}

static void LockStubPool()
{
    if (g_stubs && g_stubsNeedProt)
    {
        DWORD dummy = 0;
        VirtualProtect(g_stubs, g_stubsSize, g_stubsOldProt, &dummy);
    }
}

static void UnlockCave()
{
    if (g_cave)
        // Make cave writable so we can write vtable pointer arrays into it.
        // PAGE_READWRITE is sufficient -- the cave stores only pointer values, no code.
        VirtualProtect(g_cave, g_caveSize, PAGE_READWRITE, &g_caveOldProt);
}
static void LockCave()
{
    if (g_cave)
    {
        DWORD dummy = 0;
        // Restore original page protection for pointer-only caves.  When the same
        // cave also holds executable stubs, keep it executable after patching.
        DWORD finalProt = g_caveOldProt;
        if (g_stubsFromCave && !IsExecutableProtection(finalProt))
            finalProt = PAGE_EXECUTE_READ;
        VirtualProtect(g_cave, g_caveSize, finalProt, &dummy);
    }
}

// Generate a 24-byte stub that swaps RCX to realThis and tail-calls vtable[slot].
static void* MakeForwardStub(void* realThis, int slot)
{
    void** vtbl   = *reinterpret_cast<void***>(realThis);
    void*  target = vtbl[slot];

    BYTE* s = g_stubs + g_stubsUsed;
    g_stubsUsed += kForwardStubBytes;

    s[0]                                  = 0x48;
    s[1]                                  = 0xB9; // MOV RCX, imm64
    *reinterpret_cast<uintptr_t*>(s + 2)  = reinterpret_cast<uintptr_t>(realThis);
    s[10]                                 = 0xFF;
    s[11]                                 = 0x25; // JMP [RIP+0]
    *reinterpret_cast<DWORD*>(s + 12)     = 0;
    *reinterpret_cast<uintptr_t*>(s + 16) = reinterpret_cast<uintptr_t>(target);
    return s;
}

// ---- Shared GUIDs for QI blocking / identity checks -------------------------
// These IIDs are referenced by both WrapDev_QI and WrapCtx_QI to block
// D3D11On12/D3D12 interface exposure that would reveal the proxy to Denuvo.

static const GUID kIID_IUnknown = { 0x00000000, 0x0000, 0x0000, { 0xC0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x46 } };
static const GUID kIID_ID3D11On12Dev = {
    0x85611e73, 0x70a9, 0x490e, { 0x96, 0x14, 0xa9, 0x1e, 0x3e, 0x4f, 0x22, 0x40 }
};
static const GUID kIID_ID3D11On12Dev2 = {
    0xbdb64df4, 0xea2f, 0x4c70, { 0xb8, 0x61, 0xaa, 0xab, 0x12, 0x58, 0xbb, 0x5d }
};
static const GUID kIID_ID3D12Device = {
    0x189819f1, 0x1db6, 0x4b57, { 0xbe, 0x54, 0x18, 0x21, 0x33, 0x9b, 0x85, 0xf7 }
};
static const GUID kIID_ID3D12Device1 = {
    0x77acce80, 0x638e, 0x4e65, { 0x88, 0x95, 0xc1, 0xf2, 0x33, 0x86, 0x86, 0x3e }
};
static const GUID kIID_ID3D12Device2 = {
    0x30baa41e, 0xb15b, 0x475a, { 0xa7, 0x45, 0x7c, 0x61, 0x04, 0xa6, 0x8e, 0xf6 }
};
static const GUID kIID_ID3D12CommandQueue = {
    0x0ec870a6, 0x5d7e, 0x4c22, { 0x8c, 0xfc, 0x5b, 0xaa, 0xe0, 0x76, 0x16, 0xed }
};

// Returns true if 'riid' is a D3D11On12/D3D12 interface that must be hidden.
static bool IsBlockedD3D12IID(REFIID riid)
{
    return riid == kIID_ID3D11On12Dev || riid == kIID_ID3D11On12Dev2 || riid == kIID_ID3D12Device ||
           riid == kIID_ID3D12Device1 || riid == kIID_ID3D12Device2 || riid == kIID_ID3D12CommandQueue;
}

// ---- Device wrapper IUnknown ------------------------------------------------

static HRESULT STDMETHODCALLTYPE WrapDev_QI(IUnknown* pThis, REFIID riid, void** ppvObj)
{
    static const GUID IID_ID3D11Device_ = {
        0xDB6F6DDB, 0xAC77, 0x4E88, { 0x82, 0x53, 0x81, 0x9D, 0xF9, 0xBB, 0xF1, 0x40 }
    };
    // Denuvo probe IID (similar GUID prefix to ID3D11On12Device)
    static const GUID IID_DenuvoProbeLike_ = {
        0x85611e73, 0x70a9, 0x490e, { 0x96, 0x14, 0xa9, 0xe3, 0x02, 0x77, 0x79, 0x04 }
    };

    if (!ppvObj)
        return E_POINTER;

    // Block D3D11On12 / D3D12 detection IIDs
    if (IsBlockedD3D12IID(riid) || riid == IID_DenuvoProbeLike_)
    {
        *ppvObj = nullptr;
        return E_NOINTERFACE;
    }

    // Return the wrapper itself for the IDs we implement
    if (riid == kIID_IUnknown || riid == IID_ID3D11Device_)
    {
        *ppvObj = pThis;
        reinterpret_cast<IUnknown*>(pThis)->AddRef();
        return S_OK;
    }

    // Delegate anything else to the real clean d3d11 device (not g_on12Dev),
    // so IDXGIDevice/IDXGIObject etc. return d3d11's adapter -- no D3D12 leakage.
    //
    // When FFXV queries IDXGIDevice, intercept to lazy-patch the factory that
    // FFXV obtains via IDXGIDevice::GetAdapter()::GetParent(). This ensures our
    // swap chain hook fires even when the game bypasses ProxyFactory entirely.
    {
        static const GUID IID_IDXGIDevice_ = {
            0x54ec77fa, 0x1377, 0x44e6, { 0x8c, 0x32, 0x88, 0xfd, 0x5f, 0x44, 0xc8, 0x4c }
        };
        static const GUID IID_IDXGIDevice1_ = {
            0x77db970f, 0x6276, 0x48ba, { 0xba, 0x28, 0x07, 0x01, 0x43, 0xb4, 0x39, 0x2c }
        };
        static const GUID IID_IDXGIDevice2_ = {
            0x05008617, 0xfbfd, 0x4051, { 0xa7, 0x90, 0x14, 0x48, 0x84, 0xb4, 0xf6, 0xa9 }
        };
        if (riid == IID_IDXGIDevice_ || riid == IID_IDXGIDevice1_ || riid == IID_IDXGIDevice2_)
        {
            if (!g_cleanDev_origQI)
            {
                *ppvObj = nullptr;
                return E_NOINTERFACE;
            }
            HRESULT hr = g_cleanDev_origQI(pThis, riid, ppvObj);
            if (SUCCEEDED(hr) && *ppvObj)
            {
                // Trace adapter->GetParent to find the factory FFXV will use for swap chain creation
                auto*         dxgiDev = static_cast<IDXGIDevice*>(*ppvObj);
                IDXGIAdapter* adapter = nullptr;
                if (SUCCEEDED(dxgiDev->GetAdapter(&adapter)) && adapter)
                {
                    ComPtr<IDXGIFactory2> factory;
                    if (SUCCEEDED(adapter->GetParent(IID_PPV_ARGS(&factory))) && factory)
                    {
                        void** vt = *reinterpret_cast<void***>(factory.Get());
                        if (!g_factoryHooksInstalled)
                            InstallFactoryInlineHooks(factory.Get());
                    }
                    adapter->Release();
                }
            }
            return hr;
        }
    }
    if (g_cleanDev_origQI)
        return g_cleanDev_origQI(pThis, riid, ppvObj);
    *ppvObj = nullptr;
    return E_NOINTERFACE;
}
// Slot 40: ID3D11Device::GetImmediateContext -- return the context wrapper
static void STDMETHODCALLTYPE WrapDev_GetImmediateContext(ID3D11Device*, ID3D11DeviceContext** ppCtx)
{
    if (ppCtx)
    {
        *ppCtx = reinterpret_cast<ID3D11DeviceContext*>(g_wrapCtx);
        if (*ppCtx)
            reinterpret_cast<IUnknown*>(*ppCtx)->AddRef();
    }
}

// ---- Context wrapper IUnknown + ID3D11DeviceChild::GetDevice ----------------

static HRESULT STDMETHODCALLTYPE WrapCtx_QI(IUnknown* pThis, REFIID riid, void** ppvObj)
{
    static const GUID IID_ID3D11DevCtx_ = {
        0xC0BFA96C, 0xE089, 0x44FB, { 0x8E, 0xAF, 0x26, 0xF8, 0x79, 0x61, 0x90, 0xDA }
    };
    static const GUID IID_ID3D11DevChild_ = {
        0x1841E5C8, 0x16B0, 0x489B, { 0xBC, 0xC8, 0x44, 0xCF, 0xB0, 0xD5, 0xDE, 0xAE }
    };

    if (!ppvObj)
        return E_POINTER;

    if (IsBlockedD3D12IID(riid))
    {
        *ppvObj = nullptr;
        return E_NOINTERFACE;
    }

    if (riid == kIID_IUnknown || riid == IID_ID3D11DevCtx_ || riid == IID_ID3D11DevChild_)
    {
        *ppvObj = pThis;
        reinterpret_cast<IUnknown*>(pThis)->AddRef();
        return S_OK;
    }

    if (g_cleanCtx_origQI)
        return g_cleanCtx_origQI(pThis, riid, ppvObj);
    *ppvObj = nullptr;
    return E_NOINTERFACE;
}
// Slot 3 of ID3D11DeviceChild -- return device WRAPPER
static void STDMETHODCALLTYPE WrapCtx_GetDevice(ID3D11DeviceChild*, ID3D11Device** ppDevice)
{
    if (ppDevice)
    {
        *ppDevice = reinterpret_cast<ID3D11Device*>(g_wrapDev);
        reinterpret_cast<IUnknown*>(*ppDevice)->AddRef();
    }
}

// ---- Build wrapper vtables --------------------------------------------------
// ID3D11Device  vtable: 3 (IUnknown) + 40 (ID3D11Device)  = 43 slots
// ID3D11DeviceContext vtable: 3 (IUnknown) + 4 (DeviceChild) + 108 (DevCtx) = 115 slots
//
// Vtable POINTER ARRAYS are placed in a zero-filled loader-pad cave inside
// d3d11.dll's .data section.  device->vtable (the pointer to the array) therefore
// lives inside d3d11.dll -- passes Denuvo's module range check.
//
// The cave does NOT contain executable trampolines; only 8-byte pointer values.
// Therefore the cave page does NOT need to be executable (PAGE_READWRITE suffices),
// so no suspicious page-permission change is visible to Denuvo.
//
// Individual vtable entries point to:
//   - forwarding stubs in g_stubs (VirtualAlloc PAGE_EXECUTE_READWRITE) for
//     rendering/Create/Map/etc. calls that are forwarded to g_on12Dev/g_on12Ctx.
//   - Wrap* static functions in this DLL for the three IUnknown slots and the
//     two device<->context accessors.
//
// g_cleanD3D11Dev / g_cleanD3D11Ctx are REAL d3d11.dll COM objects whose vtable
// POINTERS are overwritten to point to our cave arrays.  All other fields in the
// COM objects retain their original d3d11.dll content -- no D3D12 pointers, no
// zeroed fields -- so Denuvo's COM object memory scan passes.

static void BuildWrappers()
{
    static const int    kVtblSlots             = 256;
    static const int    kForwardedDeviceSlots  = 69;  // Covers ID3D11Device through ID3D11Device5.
    static const int    kForwardedContextSlots = 149; // Covers ID3D11DeviceContext through ID3D11DeviceContext4.
    static const SIZE_T kCaveVtbl              = 2 * kVtblSlots * sizeof(void*);
    static const SIZE_T kStubPoolBytes =
        (((kForwardedDeviceSlots - 3) - 1) + (kForwardedContextSlots - 4)) * kForwardStubBytes;
    if (!g_cleanD3D11Dev || !g_cleanD3D11Ctx)
    {
        ProxyLog("[BuildWrappers] g_cleanD3D11Dev/Ctx not created -- cannot hook vtable");
        return;
    }

    if (!g_cave)
    {
        g_cave = FindD3D11Cave(kCaveVtbl);
        if (g_cave)
            g_caveSize = kCaveVtbl;
        else
        {
            g_cave =
                reinterpret_cast<BYTE*>(VirtualAlloc(nullptr, kCaveVtbl, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));
            if (g_cave)
                g_caveSize = kCaveVtbl;
            else
                ProxyLog("[BuildWrappers] WARNING: VirtualAlloc cave failed");
        }
    }
    if (!g_stubs)
    {
        g_stubs = FindD3D11ExecCave(kStubPoolBytes);
        if (g_stubs)
        {
            g_stubsSize     = kStubPoolBytes;
            g_stubsFromCave = true;
            g_stubsNeedProt = true;
        }
        else
            InitStubPool();
    }

    // Save original IUnknown vtable entries BEFORE hooking (MakeForwardStub reads them too)
    {
        void** v               = *reinterpret_cast<void***>(g_cleanD3D11Dev);
        g_cleanDev_origQI      = reinterpret_cast<PFN_QI>(v[0]);
        g_cleanDev_origAddRef  = reinterpret_cast<PFN_AddRef>(v[1]);
        g_cleanDev_origRelease = reinterpret_cast<PFN_Release>(v[2]);
    }
    {
        void** v               = *reinterpret_cast<void***>(g_cleanD3D11Ctx);
        g_cleanCtx_origQI      = reinterpret_cast<PFN_QI>(v[0]);
        g_cleanCtx_origAddRef  = reinterpret_cast<PFN_AddRef>(v[1]);
        g_cleanCtx_origRelease = reinterpret_cast<PFN_Release>(v[2]);
    }

    // Make cave writable (PAGE_READWRITE - no exec needed for pointer arrays)
    UnlockCave();
    UnlockStubPool();

    // Allocate 256-slot vtable arrays from cave (or fall back to static storage).
    // Using 256 slots covers all real d3d11 device/context virtual methods so that
    // d3d11's own internal virtual calls to slots 43+ still reach the original functions.
    static void* s_devVtbl[kVtblSlots] = {};
    static void* s_ctxVtbl[kVtblSlots] = {};
    void**       devVtbl               = static_cast<void**>(CaveAlloc(kVtblSlots * sizeof(void*)));
    void**       ctxVtbl               = static_cast<void**>(CaveAlloc(kVtblSlots * sizeof(void*)));
    if (!devVtbl)
        devVtbl = s_devVtbl;
    if (!ctxVtbl)
        ctxVtbl = s_ctxVtbl;

    // Seed the cave vtable with the ORIGINAL vtable entries so all slots covered.
    // Slots we override below will be patched; all others preserve original behaviour.
    void** origDevVtbl = *reinterpret_cast<void***>(g_cleanD3D11Dev);
    void** origCtxVtbl = *reinterpret_cast<void***>(g_cleanD3D11Ctx);
    for (int i = 0; i < kVtblSlots; i++) devVtbl[i] = origDevVtbl[i];
    for (int i = 0; i < kVtblSlots; i++) ctxVtbl[i] = origCtxVtbl[i];

    // ---- Device vtable ----
    // [0] QI:  custom - blocks D3D12/On12 IIDs, returns g_cleanD3D11Dev for D3D11 IIDs
    devVtbl[0] = reinterpret_cast<void*>(WrapDev_QI);
    // [1/2] AddRef/Release: forward to g_cleanD3D11Dev's ORIGINAL AddRef/Release
    // [1/2] AddRef/Release: use original d3d11 vtable entries directly.
    // MakeForwardStub(g_cleanD3D11Dev, 1/2) would forward to itself (no 'this' change
    // needed) and allocate RWX stubs unnecessarily.  Using the original entries keeps
    // these slots pointing into d3d11.dll's module range.
    devVtbl[1] = origDevVtbl[1]; // original d3d11 AddRef
    devVtbl[2] = origDevVtbl[2]; // original d3d11 Release
    // Forward every remaining device slot to the D3D11On12 device so extension
    // interfaces obtained via QueryInterface stay on the D3D11On12 path instead
    // of mixing the clean D3D11 implementation with D3D11On12-backed objects.
    for (int i = 3; i < kForwardedDeviceSlots; i++)
    {
        if (i != kDevGetImmCtxVtIdx)
            devVtbl[i] = MakeForwardStub(g_on12Dev, i);
    }
    // [kDevGetImmCtxVtIdx] GetImmediateContext: return the hooked context (g_cleanD3D11Ctx)
    devVtbl[kDevGetImmCtxVtIdx] = reinterpret_cast<void*>(WrapDev_GetImmediateContext);

    // ---- Context vtable ----
    ctxVtbl[0] = reinterpret_cast<void*>(WrapCtx_QI);
    // [1/2] AddRef/Release: same reasoning -- use originals, no RWX stubs needed.
    ctxVtbl[1] = origCtxVtbl[1]; // original d3d11 AddRef
    ctxVtbl[2] = origCtxVtbl[2]; // original d3d11 Release
    // [3] GetDevice (ID3D11DeviceChild): return the hooked device (g_cleanD3D11Dev)
    ctxVtbl[3] = reinterpret_cast<void*>(WrapCtx_GetDevice);
    for (int i = 4; i < kForwardedContextSlots; i++) ctxVtbl[i] = MakeForwardStub(g_on12Ctx, i);

    // Restore page protection on cave (back to original, typically PAGE_WRITECOPY)
    LockCave();
    LockStubPool();

    // Overwrite vtable POINTERS in the real COM objects.
    // These objects live in d3d11.dll's heap (PAGE_READWRITE) so no VirtualProtect needed.
    *reinterpret_cast<void***>(g_cleanD3D11Dev) = devVtbl;
    *reinterpret_cast<void***>(g_cleanD3D11Ctx) = ctxVtbl;

    // Alias g_wrapDev/g_wrapCtx to the real objects for compatibility with helper code
    // that casts them back to ID3D11Device*/ID3D11DeviceContext*.
    g_wrapDev = reinterpret_cast<WrapperObj*>(g_cleanD3D11Dev);
    g_wrapCtx = reinterpret_cast<WrapperObj*>(g_cleanD3D11Ctx);

    ProxyLog("[BuildWrappers] dev=%p ctx=%p stubs=%p used=%zu devVtbl=%p cave=%p (fromCave=%d)",
             (void*)g_wrapDev,
             (void*)g_wrapCtx,
             (void*)g_stubs,
             g_stubsUsed,
             (void*)devVtbl,
             (void*)g_cave,
             (int)g_stubsFromCave);
}

static HRESULT WINAPI HookD3D11CreateDevice(IDXGIAdapter*            pAdapter,
                                            D3D_DRIVER_TYPE          DriverType,
                                            HMODULE                  Software,
                                            UINT                     Flags,
                                            const D3D_FEATURE_LEVEL* pFeatureLevels,
                                            UINT                     FeatureLevels,
                                            UINT                     SDKVersion,
                                            ID3D11Device**           ppDevice,
                                            D3D_FEATURE_LEVEL*       pFeatureLevel,
                                            ID3D11DeviceContext**    ppImmediateContext)
{
    ProxyLog("[HookD3D11CreateDevice] DriverType=%d Flags=0x%X SDK=%u", (int)DriverType, Flags, SDKVersion);

    if (g_creatingDevice)
    {
        // Re-entrant call from D3D11On12CreateDevice -- use system D3D11
        return CallRealD3D11CreateDevice(pAdapter,
                                         DriverType,
                                         Software,
                                         Flags,
                                         pFeatureLevels,
                                         FeatureLevels,
                                         SDKVersion,
                                         ppDevice,
                                         pFeatureLevel,
                                         ppImmediateContext);
    }

    // Pass through non-hardware device types (WARP, reference, software) unchanged.
    // D3D11On12 only supports hardware adapters; D3D_DRIVER_TYPE_UNKNOWN with an explicit
    // adapter is allowed (game knows the hardware adapter).
    if (DriverType != D3D_DRIVER_TYPE_UNKNOWN && DriverType != D3D_DRIVER_TYPE_HARDWARE)
        return CallRealD3D11CreateDevice(pAdapter,
                                         DriverType,
                                         Software,
                                         Flags,
                                         pFeatureLevels,
                                         FeatureLevels,
                                         SDKVersion,
                                         ppDevice,
                                         pFeatureLevel,
                                         ppImmediateContext);

    // Diagnostic passthrough: if proxy_passthrough.txt exists in the EXE directory,
    // skip D3D11On12 and use the real D3D11 device instead.
    {
        static bool           s_passthrough = false;
        static std::once_flag s_once;
        std::call_once(s_once, [] {
            wchar_t buf[MAX_PATH];
            swprintf_s(buf, L"%sproxy_passthrough.txt", g_exeDir);
            s_passthrough = (GetFileAttributesW(buf) != INVALID_FILE_ATTRIBUTES);
        });
        if (s_passthrough)
        {
            RestoreIAT_D3D11CreateDevice();
            auto realFn = reinterpret_cast<PFN_D3D11CreateDevice_t>(GetProcAddress(g_hD3D11Sys, "D3D11CreateDevice"));
            return realFn ? realFn(pAdapter,
                                   DriverType,
                                   Software,
                                   Flags,
                                   pFeatureLevels,
                                   FeatureLevels,
                                   SDKVersion,
                                   ppDevice,
                                   pFeatureLevel,
                                   ppImmediateContext)
                          : DXGI_ERROR_UNSUPPORTED;
        }
    }

    if (!g_D3D12CreateDevice || !g_D3D11On12Create)
        return CallRealD3D11CreateDevice(pAdapter,
                                         DriverType,
                                         Software,
                                         Flags,
                                         pFeatureLevels,
                                         FeatureLevels,
                                         SDKVersion,
                                         ppDevice,
                                         pFeatureLevel,
                                         ppImmediateContext);

    g_creatingDevice = true;

    // 1. Create D3D12 device
    ComPtr<ID3D12Device> d3d12Dev;
    D3D_FEATURE_LEVEL    featureLevel = D3D_FEATURE_LEVEL_11_0;
    HRESULT              hr           = g_D3D12CreateDevice(
        pAdapter ? static_cast<IUnknown*>(pAdapter) : nullptr, D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&d3d12Dev));
    if (FAILED(hr))
    {
        g_creatingDevice = false;
        ProxyLog("[HookD3D11CreateDevice] D3D12CreateDevice hr=0x%08X", (unsigned)hr);
        return CallRealD3D11CreateDevice(pAdapter,
                                         DriverType,
                                         Software,
                                         Flags,
                                         pFeatureLevels,
                                         FeatureLevels,
                                         SDKVersion,
                                         ppDevice,
                                         pFeatureLevel,
                                         ppImmediateContext);
    }

    // 2. Create D3D12 command queue
    ComPtr<ID3D12CommandQueue> cmdQueue;
    D3D12_COMMAND_QUEUE_DESC   qDesc = {};
    qDesc.Type                       = D3D12_COMMAND_LIST_TYPE_DIRECT;
    qDesc.Flags                      = D3D12_COMMAND_QUEUE_FLAG_NONE;
    hr                               = d3d12Dev->CreateCommandQueue(&qDesc, IID_PPV_ARGS(&cmdQueue));
    if (FAILED(hr))
    {
        g_creatingDevice = false;
        ProxyLog("[HookD3D11CreateDevice] CreateCommandQueue hr=0x%08X", (unsigned)hr);
        return hr;
    }
    UINT d3d11Flags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;
    if (Flags & D3D11_CREATE_DEVICE_DEBUG)
        d3d11Flags |= D3D11_CREATE_DEVICE_DEBUG;

    static const D3D_FEATURE_LEVEL kFeatureLevels[] = {
        D3D_FEATURE_LEVEL_11_1,
        D3D_FEATURE_LEVEL_11_0,
    };
    const D3D_FEATURE_LEVEL* pFL = pFeatureLevels ? pFeatureLevels : kFeatureLevels;
    UINT                     nFL = pFeatureLevels ? FeatureLevels : ARRAYSIZE(kFeatureLevels);

    IUnknown*                   queues[] = { cmdQueue.Get() };
    ComPtr<ID3D11Device>        d3d11Dev;
    ComPtr<ID3D11DeviceContext> d3d11Ctx;
    hr = g_D3D11On12Create(d3d12Dev.Get(), d3d11Flags, pFL, nFL, queues, 1, 0, &d3d11Dev, &d3d11Ctx, &featureLevel);
    g_creatingDevice = false;

    if (FAILED(hr))
    {
        ProxyLog("[HookD3D11CreateDevice] D3D11On12CreateDevice hr=0x%08X", (unsigned)hr);
        return CallRealD3D11CreateDevice(pAdapter,
                                         DriverType,
                                         Software,
                                         Flags,
                                         pFeatureLevels,
                                         FeatureLevels,
                                         SDKVersion,
                                         ppDevice,
                                         pFeatureLevel,
                                         ppImmediateContext);
    }

    ComPtr<ID3D11Multithread> multithread;
    if (SUCCEEDED(d3d11Ctx.As(&multithread)) && multithread)
        multithread->SetMultithreadProtected(TRUE);

    // Hide d3d11on12.dll from PEB module list so Denuvo's module-enumeration integrity
    // check cannot detect its presence.  The DLL remains loaded and functional.
    if (g_denuvoMode)
    {
        HMODULE hOn12 = GetModuleHandleW(L"d3d11on12.dll");
        UnlinkDllFromPEB(hOn12);
    }

    // 4. Save real D3D11On12 objects.
    d3d11Dev->QueryInterface(IID_PPV_ARGS(&g_on12Interface));
    g_on12Dev = d3d11Dev.Get();
    g_on12Dev->AddRef();
    g_on12Ctx = d3d11Ctx.Get();
    g_on12Ctx->AddRef();

    auto StoreDeviceContext = [&](ID3D11Device* key) {
        auto ctx             = std::make_shared<DeviceContext>();
        ctx->d3d12Device     = d3d12Dev;
        ctx->cmdQueue        = cmdQueue;
        ctx->d3d11On12Device = g_on12Interface;
        ctx->d3d11Context    = d3d11Ctx;
        AcquireSRWLockExclusive(&g_deviceContextLock);
        g_deviceContexts[key] = ctx;
        ReleaseSRWLockExclusive(&g_deviceContextLock);
    };

    if (g_denuvoMode)
    {
        // Denuvo mode: create a "clean" real D3D11 device, overlay our vtable, return it.
        // This prevents Denuvo's COM object memory scan from detecting D3D11On12.

        // Remove hooks before creating the clean device so GetProcAddress returns the
        // real D3D11CreateDevice without going through our hook.
        RestoreIAT_D3D11CreateDevice();

        // 4b. Create clean (real) D3D11 device; pFL/nFL already computed above.
        // g_inlineHookTarget is now null (hook removed); GetProcAddress returns real fn.
        {
            D3D_FEATURE_LEVEL cleanFL = D3D_FEATURE_LEVEL_11_0;
            auto realFn  = reinterpret_cast<PFN_D3D11CreateDevice_t>(GetProcAddress(g_hD3D11Sys, "D3D11CreateDevice"));
            HRESULT hr4b = E_FAIL;
            if (realFn)
                hr4b = realFn(pAdapter,
                              DriverType,
                              Software,
                              Flags,
                              pFL,
                              nFL,
                              SDKVersion,
                              &g_cleanD3D11Dev,
                              &cleanFL,
                              &g_cleanD3D11Ctx);
            ProxyLog("[HookD3D11CreateDevice] clean device hr=0x%08X dev=%p ctx=%p",
                     (unsigned)hr4b,
                     (void*)g_cleanD3D11Dev,
                     (void*)g_cleanD3D11Ctx);
        }

        BuildWrappers();

        // 5. Store DeviceContext keyed on wrapper device pointer.
        StoreDeviceContext(reinterpret_cast<ID3D11Device*>(g_wrapDev));

        ProxyLog("[HookD3D11CreateDevice] OK (Denuvo) wrap=%p on12=%p d3d12=%p clean=%p",
                 (void*)g_wrapDev,
                 (void*)g_on12Dev,
                 (void*)d3d12Dev.Get(),
                 (void*)g_cleanD3D11Dev);

        // Hooks already removed above -- no second call needed.

        if (ppDevice)
        {
            *ppDevice = reinterpret_cast<ID3D11Device*>(g_wrapDev);
        }
        if (ppImmediateContext)
        {
            *ppImmediateContext = reinterpret_cast<ID3D11DeviceContext*>(g_wrapCtx);
        }
        if (pFeatureLevel)
        {
            *pFeatureLevel = featureLevel;
        }
    }
    else
    {
        // Default mode: return the D3D11On12 device directly -- no vtable tricks.
        // The D3D11On12 device IS a valid ID3D11Device; non-Denuvo apps will work fine.
        // 5. Store DeviceContext keyed on the D3D11On12 device pointer.
        StoreDeviceContext(d3d11Dev.Get());

        ProxyLog("[HookD3D11CreateDevice] OK (direct) dev=%p ctx=%p d3d12=%p",
                 (void*)d3d11Dev.Get(),
                 (void*)d3d11Ctx.Get(),
                 (void*)d3d12Dev.Get());

        // If the app created its DXGI factory before d3d11.dll was loaded, our
        // WrapInProxy path never ran and the factory inline hooks were never installed.
        // Fix: retrieve the system factory via the D3D11On12 device's DXGI adapter
        // chain (same technique as Denuvo's WrapDev_QI) and install the hooks now.
        // InstallFactoryInlineHooks patches the function CODE (inline hook), so it
        // applies globally to all factory instances -- including FFXV's pre-existing factory.
        if (!g_factoryHooksInstalled)
        {
            ComPtr<IDXGIDevice> dxgiDev;
            if (SUCCEEDED(d3d11Dev->QueryInterface(IID_PPV_ARGS(&dxgiDev))))
            {
                ComPtr<IDXGIAdapter> adapter;
                if (SUCCEEDED(dxgiDev->GetAdapter(&adapter)) && adapter)
                {
                    ComPtr<IDXGIFactory2> factory;
                    if (SUCCEEDED(adapter->GetParent(IID_PPV_ARGS(&factory))) && factory)
                    {
                        InstallFactoryInlineHooks(factory.Get());
                        // Use this factory as the fallback capture factory if not yet set.
                        if (!g_captureFactory)
                            g_captureFactory = factory;
                    }
                }
            }
        }

        // Keep IAT hook active so subsequent D3D11CreateDevice calls are also intercepted.
        if (ppDevice)
        {
            *ppDevice = d3d11Dev.Detach();
        }
        if (ppImmediateContext)
        {
            *ppImmediateContext = d3d11Ctx.Detach();
        }
        if (pFeatureLevel)
        {
            *pFeatureLevel = featureLevel;
        }
    }
    return S_OK;
}

// =============================================================================
// ProxyFactory: IDXGIFactory7 wrapper
// =============================================================================

class ProxyFactory final : public IDXGIFactory7
{
    volatile LONG         m_refCount{ 1 };
    ComPtr<IDXGIFactory2> m_real;
    ComPtr<IDXGIFactory3> m_real3;
    ComPtr<IDXGIFactory4> m_real4;
    ComPtr<IDXGIFactory5> m_real5;
    ComPtr<IDXGIFactory6> m_real6;
    ComPtr<IDXGIFactory7> m_real7;

  public:
    explicit ProxyFactory(IDXGIFactory2* real) : m_real(real)
    {
        real->QueryInterface(IID_PPV_ARGS(&m_real3));
        real->QueryInterface(IID_PPV_ARGS(&m_real4));
        real->QueryInterface(IID_PPV_ARGS(&m_real5));
        real->QueryInterface(IID_PPV_ARGS(&m_real6));
        real->QueryInterface(IID_PPV_ARGS(&m_real7));
    }

    // -------------------------------------------------------------------------
    // IUnknown
    // -------------------------------------------------------------------------

    ULONG STDMETHODCALLTYPE AddRef() override { return InterlockedIncrement(&m_refCount); }

    ULONG STDMETHODCALLTYPE Release() override
    {
        ULONG r = InterlockedDecrement(&m_refCount);
        if (r == 0)
            delete this;
        return r;
    }

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppv) override
    {
        if (!ppv)
            return E_POINTER;
        // Return proxy for all IDXGIFactory levels so we keep interception.
        if (riid == __uuidof(IUnknown) || riid == __uuidof(IDXGIObject) || riid == __uuidof(IDXGIFactory) ||
            riid == __uuidof(IDXGIFactory1) || riid == __uuidof(IDXGIFactory2))
        {
            *ppv = static_cast<IDXGIFactory2*>(this);
            AddRef();
            return S_OK;
        }
        if ((riid == __uuidof(IDXGIFactory3) && m_real3) || (riid == __uuidof(IDXGIFactory4) && m_real4) ||
            (riid == __uuidof(IDXGIFactory5) && m_real5) || (riid == __uuidof(IDXGIFactory6) && m_real6) ||
            (riid == __uuidof(IDXGIFactory7) && m_real7))
        {
            *ppv = static_cast<IDXGIFactory7*>(this);
            AddRef();
            return S_OK;
        }
        return m_real->QueryInterface(riid, ppv);
    }

    // -------------------------------------------------------------------------
    // IDXGIObject
    // -------------------------------------------------------------------------

    HRESULT STDMETHODCALLTYPE SetPrivateData(REFGUID Name, UINT DataSize, const void* pData) override
    {
        return m_real->SetPrivateData(Name, DataSize, pData);
    }
    HRESULT STDMETHODCALLTYPE SetPrivateDataInterface(REFGUID Name, const IUnknown* pUnk) override
    {
        return m_real->SetPrivateDataInterface(Name, pUnk);
    }
    HRESULT STDMETHODCALLTYPE GetPrivateData(REFGUID Name, UINT* pDataSize, void* pData) override
    {
        return m_real->GetPrivateData(Name, pDataSize, pData);
    }
    HRESULT STDMETHODCALLTYPE GetParent(REFIID riid, void** ppParent) override
    {
        return m_real->GetParent(riid, ppParent);
    }

    // -------------------------------------------------------------------------
    // IDXGIFactory
    // -------------------------------------------------------------------------

    HRESULT STDMETHODCALLTYPE EnumAdapters(UINT Adapter, IDXGIAdapter** ppAdapter) override
    {
        return m_real->EnumAdapters(Adapter, ppAdapter);
    }
    HRESULT STDMETHODCALLTYPE MakeWindowAssociation(HWND WindowHandle, UINT Flags) override
    {
        return m_real->MakeWindowAssociation(WindowHandle, Flags);
    }
    HRESULT STDMETHODCALLTYPE GetWindowAssociation(HWND* pWindowHandle) override
    {
        return m_real->GetWindowAssociation(pWindowHandle);
    }

    HRESULT STDMETHODCALLTYPE CreateSwapChain(IUnknown*             pDevice,
                                              DXGI_SWAP_CHAIN_DESC* pDesc,
                                              IDXGISwapChain**      ppSwapChain) override
    {
        if (!pDevice || !pDesc || !ppSwapChain)
            return m_real->CreateSwapChain(pDevice, pDesc, ppSwapChain);

        DXGI_SWAP_CHAIN_DESC1 desc1 = {};
        ConvertDesc(*pDesc, desc1);

        ComPtr<IDXGISwapChain1> sc1;
        HRESULT                 hr =
            MakeProxySwapChain(pDevice, m_real.Get(), pDesc->OutputWindow, &desc1, nullptr, nullptr, pDesc, &sc1);
        if (SUCCEEDED(hr))
        {
            *ppSwapChain = sc1.Detach();
            return S_OK;
        }
        ProxyLog("[ProxyFactory::CreateSwapChain] MakeProxySwapChain hr=0x%08X -> fallback", (unsigned)hr);
        return m_real->CreateSwapChain(pDevice, pDesc, ppSwapChain);
    }

    HRESULT STDMETHODCALLTYPE CreateSoftwareAdapter(HMODULE Module, IDXGIAdapter** ppAdapter) override
    {
        return m_real->CreateSoftwareAdapter(Module, ppAdapter);
    }

    // -------------------------------------------------------------------------
    // IDXGIFactory1
    // -------------------------------------------------------------------------

    HRESULT STDMETHODCALLTYPE EnumAdapters1(UINT Adapter, IDXGIAdapter1** ppAdapter) override
    {
        return m_real->EnumAdapters1(Adapter, ppAdapter);
    }
    BOOL STDMETHODCALLTYPE IsCurrent() override { return m_real->IsCurrent(); }

    // -------------------------------------------------------------------------
    // IDXGIFactory2
    // -------------------------------------------------------------------------

    BOOL STDMETHODCALLTYPE IsWindowedStereoEnabled() override { return m_real->IsWindowedStereoEnabled(); }

    HRESULT STDMETHODCALLTYPE CreateSwapChainForHwnd(IUnknown*                              pDevice,
                                                     HWND                                   hWnd,
                                                     const DXGI_SWAP_CHAIN_DESC1*           pDesc,
                                                     const DXGI_SWAP_CHAIN_FULLSCREEN_DESC* pFsDesc,
                                                     IDXGIOutput*                           pRestrictToOutput,
                                                     IDXGISwapChain1**                      ppSwapChain) override
    {
        if (!pDevice || !pDesc || !ppSwapChain || !hWnd)
            return m_real->CreateSwapChainForHwnd(pDevice, hWnd, pDesc, pFsDesc, pRestrictToOutput, ppSwapChain);

        DXGI_SWAP_CHAIN_DESC origDesc = BuildLegacyDesc(hWnd, pDesc, pFsDesc);
        HRESULT              hr =
            MakeProxySwapChain(pDevice, m_real.Get(), hWnd, pDesc, pFsDesc, pRestrictToOutput, &origDesc, ppSwapChain);
        if (SUCCEEDED(hr))
            return S_OK;
        ProxyLog("[ProxyFactory::CreateSwapChainForHwnd] MakeProxySwapChain hr=0x%08X -> fallback", (unsigned)hr);
        return m_real->CreateSwapChainForHwnd(pDevice, hWnd, pDesc, pFsDesc, pRestrictToOutput, ppSwapChain);
    }

    HRESULT STDMETHODCALLTYPE CreateSwapChainForCoreWindow(IUnknown*                    pDevice,
                                                           IUnknown*                    pWindow,
                                                           const DXGI_SWAP_CHAIN_DESC1* pDesc,
                                                           IDXGIOutput*                 pRestrictToOutput,
                                                           IDXGISwapChain1**            ppSwapChain) override
    {
        return m_real->CreateSwapChainForCoreWindow(pDevice, pWindow, pDesc, pRestrictToOutput, ppSwapChain);
    }

    HRESULT STDMETHODCALLTYPE GetSharedResourceAdapterLuid(HANDLE hResource, LUID* pLuid) override
    {
        return m_real->GetSharedResourceAdapterLuid(hResource, pLuid);
    }
    HRESULT STDMETHODCALLTYPE RegisterStereoStatusWindow(HWND wnd, UINT msg, DWORD* pdwCookie) override
    {
        return m_real->RegisterStereoStatusWindow(wnd, msg, pdwCookie);
    }
    HRESULT STDMETHODCALLTYPE RegisterStereoStatusEvent(HANDLE hEvent, DWORD* pdwCookie) override
    {
        return m_real->RegisterStereoStatusEvent(hEvent, pdwCookie);
    }
    void STDMETHODCALLTYPE UnregisterStereoStatus(DWORD dwCookie) override { m_real->UnregisterStereoStatus(dwCookie); }
    HRESULT STDMETHODCALLTYPE RegisterOcclusionStatusWindow(HWND wnd, UINT msg, DWORD* pdwCookie) override
    {
        return m_real->RegisterOcclusionStatusWindow(wnd, msg, pdwCookie);
    }
    HRESULT STDMETHODCALLTYPE RegisterOcclusionStatusEvent(HANDLE hEvent, DWORD* pdwCookie) override
    {
        return m_real->RegisterOcclusionStatusEvent(hEvent, pdwCookie);
    }
    void STDMETHODCALLTYPE UnregisterOcclusionStatus(DWORD dwCookie) override
    {
        m_real->UnregisterOcclusionStatus(dwCookie);
    }
    HRESULT STDMETHODCALLTYPE CreateSwapChainForComposition(IUnknown*                    pDevice,
                                                            const DXGI_SWAP_CHAIN_DESC1* pDesc,
                                                            IDXGIOutput*                 pRestrictToOutput,
                                                            IDXGISwapChain1**            ppSwapChain) override
    {
        return m_real->CreateSwapChainForComposition(pDevice, pDesc, pRestrictToOutput, ppSwapChain);
    }

    // -------------------------------------------------------------------------
    // IDXGIFactory3
    // -------------------------------------------------------------------------

    UINT STDMETHODCALLTYPE GetCreationFlags() override { return m_real3 ? m_real3->GetCreationFlags() : 0; }

    // -------------------------------------------------------------------------
    // IDXGIFactory4
    // -------------------------------------------------------------------------

    HRESULT STDMETHODCALLTYPE EnumAdapterByLuid(LUID AdapterLuid, REFIID riid, void** ppvAdapter) override
    {
        return m_real4 ? m_real4->EnumAdapterByLuid(AdapterLuid, riid, ppvAdapter) : DXGI_ERROR_NOT_FOUND;
    }
    HRESULT STDMETHODCALLTYPE EnumWarpAdapter(REFIID riid, void** ppvAdapter) override
    {
        return m_real4 ? m_real4->EnumWarpAdapter(riid, ppvAdapter) : DXGI_ERROR_NOT_FOUND;
    }

    // -------------------------------------------------------------------------
    // IDXGIFactory5
    // -------------------------------------------------------------------------

    HRESULT STDMETHODCALLTYPE CheckFeatureSupport(DXGI_FEATURE Feature,
                                                  void*        pFeatureSupportData,
                                                  UINT         FeatureSupportDataSize) override
    {
        return m_real5 ? m_real5->CheckFeatureSupport(Feature, pFeatureSupportData, FeatureSupportDataSize)
                       : DXGI_ERROR_INVALID_CALL;
    }

    // -------------------------------------------------------------------------
    // IDXGIFactory6
    // -------------------------------------------------------------------------

    HRESULT STDMETHODCALLTYPE EnumAdapterByGpuPreference(UINT                Adapter,
                                                         DXGI_GPU_PREFERENCE GpuPreference,
                                                         REFIID              riid,
                                                         void**              ppvAdapter) override
    {
        return m_real6 ? m_real6->EnumAdapterByGpuPreference(Adapter, GpuPreference, riid, ppvAdapter)
                       : DXGI_ERROR_NOT_FOUND;
    }

    // -------------------------------------------------------------------------
    // IDXGIFactory7
    // -------------------------------------------------------------------------

    HRESULT STDMETHODCALLTYPE RegisterAdaptersChangedEvent(HANDLE hEvent, DWORD* pdwCookie) override
    {
        return m_real7 ? m_real7->RegisterAdaptersChangedEvent(hEvent, pdwCookie) : E_NOTIMPL;
    }
    HRESULT STDMETHODCALLTYPE UnregisterAdaptersChangedEvent(DWORD dwCookie) override
    {
        return m_real7 ? m_real7->UnregisterAdaptersChangedEvent(dwCookie) : E_NOTIMPL;
    }
};

// =============================================================================
// Helper: wrap any IDXGIFactory* in a ProxyFactory
// =============================================================================

static HRESULT WrapInProxy(REFIID /*riid*/, void** ppFactory)
{
    if (!ppFactory || !*ppFactory)
        return S_OK;
    // Take ownership of the factory reference returned by the real CreateDXGIFactory*.
    // We need IDXGIFactory2 to build our ProxyFactory.
    ComPtr<IDXGIFactory2> f2;
    {
        IUnknown* orig = static_cast<IUnknown*>(*ppFactory);
        HRESULT   qiHr = orig->QueryInterface(IID_PPV_ARGS(&f2));
        orig->Release(); // release the ref that came from CreateDXGIFactory*
        if (FAILED(qiHr))
            return S_OK; // factory already released; leave *ppFactory as stale (callers check hr)
    }
    // Save the gfxrecon capture factory so MakeProxySwapChain can always route
    // D3D12 swap chain creation through it regardless of which factory the app obtained.
    if (!g_captureFactory)
        g_captureFactory = f2;
    ProxyFactory* proxy = new ProxyFactory(f2.Get());
    *ppFactory          = static_cast<IDXGIFactory2*>(proxy);
    return S_OK;
}

// =============================================================================
// Public API -- called from gfxrecon dxgi.dll
// =============================================================================

void D3D11On12Proxy_Init(HINSTANCE hDLL)
{
    // Compute DLL directory for log path.
    {
        wchar_t dllPath[MAX_PATH] = {};
        GetModuleFileNameW(hDLL, dllPath, MAX_PATH);
        wchar_t* p = wcsrchr(dllPath, L'\\');
        if (p)
        {
            wcsncpy_s(g_dllDir, dllPath, (p - dllPath) + 1); // includes trailing backslash
            char dllDirA[MAX_PATH] = {};
            WideCharToMultiByte(CP_ACP, 0, g_dllDir, -1, dllDirA, MAX_PATH, nullptr, nullptr);
            sprintf_s(g_logPath, "%sd3d11proxy.log", dllDirA);
        }
    }

    // Compute EXE directory.
    {
        wchar_t exePath[MAX_PATH] = {};
        GetModuleFileNameW(nullptr, exePath, MAX_PATH);
        wchar_t* p = wcsrchr(exePath, L'\\');
        if (p)
            wcsncpy_s(g_exeDir, exePath, (p - exePath) + 1);
    }

    // Check Denuvo mode: auto-detect by scanning the main EXE's PE sections for
    // Denuvo's characteristic ".arch" section (virtualized code), or fall back to
    // manual override via proxy_denuvo.txt in the EXE directory.
    {
        // Auto-detect: scan EXE PE headers for Denuvo's ".arch" section.
        HMODULE hExe = GetModuleHandleW(nullptr);
        if (hExe)
        {
            auto* base = reinterpret_cast<const BYTE*>(hExe);
            auto* dos  = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
            if (dos->e_magic == IMAGE_DOS_SIGNATURE)
            {
                auto* nt  = reinterpret_cast<const IMAGE_NT_HEADERS64*>(base + dos->e_lfanew);
                auto* sec = IMAGE_FIRST_SECTION(nt);
                for (WORD i = 0; i < nt->FileHeader.NumberOfSections; i++)
                {
                    char name[kImageSectionNameLen + 1] = {};
                    GetSectionName(sec[i], name);
                    if (strcmp(name, ".arch") == 0)
                    {
                        g_denuvoMode = true;
                        ProxyLog("[D3D11On12Proxy] Denuvo auto-detected: .arch section found in EXE");
                        break;
                    }
                }
            }
        }

        // Manual override: proxy_denuvo.txt in EXE directory (always wins).
        if (!g_denuvoMode)
        {
            wchar_t probe[MAX_PATH] = {};
            swprintf_s(probe, L"%sproxy_denuvo.txt", g_exeDir);
            if (GetFileAttributesW(probe) != INVALID_FILE_ATTRIBUTES)
            {
                g_denuvoMode = true;
                ProxyLog("[D3D11On12Proxy] Denuvo mode enabled via proxy_denuvo.txt");
            }
        }
    }

    // Enable D3D11On12 capture:
    //   - Denuvo mode: always enable (explicit trigger required for Denuvo bypass logic).
    //   - Otherwise: deferred to first D3D11On12Proxy_WrapFactory call, where we
    //     dynamically detect whether d3d11.dll is loaded in the process.
    //     If d3d11.dll is loaded -> D3D11 app -> enable.
    //     If not -> native DX12 app -> do nothing.
    if (g_denuvoMode)
    {
        g_d3d11on12Enabled = true;
    }

    ProxyLog("[D3D11On12Proxy] Init d3d11on12Enabled=%d denuvoMode=%d", (int)g_d3d11on12Enabled, (int)g_denuvoMode);
    // Diagnostic handlers (VEH) are installed lazily in D3D11On12Proxy_WrapFactory
    // when we confirm this is a D3D11 app.  Pure D3D12 apps must not trigger them.
    if (g_d3d11on12Enabled)
    {
        // Denuvo mode was already confirmed at Init time -- install now.
        InstallCrashFilter();
    }
    // D3D12 loading and IAT patching are deferred to first factory call
    // (LazyInitD3D12) to avoid triggering Denuvo's early startup checks.
}

void D3D11On12Proxy_Destroy()
{
    RemoveFactoryInlineHooks();
    if (g_vehHandle)
    {
        RemoveVectoredExceptionHandler(g_vehHandle);
        g_vehHandle = nullptr;
    }
    if (g_hD3D12)
    {
        FreeLibrary(g_hD3D12);
        g_hD3D12 = nullptr;
    }
    if (g_hD3D11Sys)
    {
        FreeLibrary(g_hD3D11Sys);
        g_hD3D11Sys = nullptr;
    }
}

// Returns true if the main EXE's static import table references d3d11.dll.
// This reliably identifies D3D11 apps: their EXE imports D3D11CreateDevice (or other
// d3d11.dll functions) directly. Pure D3D12 apps only import d3d12.dll/dxgi.dll from
// the graphics stack, even when d3d11.dll happens to be loaded by a system component,
// debug layer, or injected DLL.
static bool MainExeImportsFromD3D11()
{
    HMODULE hExe = GetModuleHandleW(nullptr);
    if (!hExe)
        return false;
    auto* base = reinterpret_cast<const BYTE*>(hExe);
    auto* dos  = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
    if (dos->e_magic != IMAGE_DOS_SIGNATURE)
        return false;
    auto* nt     = reinterpret_cast<const IMAGE_NT_HEADERS*>(base + dos->e_lfanew);
    auto& impDir = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
    if (!impDir.VirtualAddress)
        return false;
    auto* desc = reinterpret_cast<const IMAGE_IMPORT_DESCRIPTOR*>(base + impDir.VirtualAddress);
    for (; desc->Name; ++desc)
    {
        const char* dllName = reinterpret_cast<const char*>(base + desc->Name);
        if (_stricmp(dllName, "d3d11.dll") == 0)
            return true;
    }
    return false;
}

void D3D11On12Proxy_WrapFactory(void** ppFactory)
{
    // Dynamic detection: check the main EXE's static import table for d3d11.dll.
    // This is more reliable than GetModuleHandleW: D3D12 apps never import d3d11.dll
    // in their own IAT, even when d3d11.dll is loaded by a debug layer or injected DLL.
    if (!g_d3d11on12Enabled)
    {
        if (MainExeImportsFromD3D11())
        {
            g_d3d11on12Enabled = true;
            ProxyLog("[D3D11On12Proxy] Auto-enabled: d3d11.dll found in EXE IAT");
            // Install crash filter only now that we know it's a D3D11 app.
            // Pure D3D12 apps must not have this handler.
            InstallCrashFilter();
        }
    }
    if (!g_d3d11on12Enabled)
        return;
    if (!ppFactory || !*ppFactory || g_creatingDevice)
        return;
    if (!g_D3D12CreateDevice)
        LazyInitD3D12();
    WrapInProxy(IID_IUnknown, ppFactory);
}
