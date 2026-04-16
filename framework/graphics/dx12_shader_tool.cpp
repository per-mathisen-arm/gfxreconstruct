/*
** Copyright (c) 2025 LunarG, Inc.
** Copyright (c) 2025-2026 Arm Limited and/or its affiliates <open-source-office@arm.com>
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

#include "graphics/dx12_shader_tool.h"

#include "util/file_path.h"
#include "util/logging.h"
#include "util/platform.h"

#include <cinttypes>
#include <cstring>

#include <d3d12.h>
#include <d3d12shader.h>
#include <wrl/client.h>

#if defined(GFXRECON_DXC_SUPPORT)
#include <dxcapi.h>
#endif

#if defined(WIN32)
#include <d3dcompiler.h>
#pragma comment(lib, "d3dcompiler.lib")
#endif

GFXRECON_BEGIN_NAMESPACE(gfxrecon)
GFXRECON_BEGIN_NAMESPACE(graphics)

const char* Dx12ShaderTool::ShaderTypeToString(ShaderType type)
{
    switch (type)
    {
        case ShaderType::kVertex:
            return "vertex";
        case ShaderType::kDomain:
            return "domain";
        case ShaderType::kHull:
            return "hull";
        case ShaderType::kGeometry:
            return "geometry";
        case ShaderType::kPixel:
            return "pixel";
        case ShaderType::kCompute:
            return "compute";
        case ShaderType::kStateObjectDxilLibrary:
            return "dxil_library";
        default:
            return "unknown";
    }
}

std::string Dx12ShaderTool::MakePipelineShaderFileName(uint64_t handle_id, ShaderType type)
{
    std::string suffix;
    switch (type)
    {
        case ShaderType::kVertex:
            suffix = ".vs.cso";
            break;
        case ShaderType::kPixel:
            suffix = ".ps.cso";
            break;
        case ShaderType::kDomain:
            suffix = ".ds.cso";
            break;
        case ShaderType::kHull:
            suffix = ".hs.cso";
            break;
        case ShaderType::kGeometry:
            suffix = ".gs.cso";
            break;
        case ShaderType::kCompute:
            suffix = ".cs.cso";
            break;
        default:
            suffix = ".cso";
            break;
    }

    return "sh" + std::to_string(handle_id) + suffix;
}

std::string Dx12ShaderTool::MakeStateObjectDxilLibraryFileName(uint64_t handle_id, uint32_t subobject_index)
{
    return "sh" + std::to_string(handle_id) + "_" + std::to_string(subobject_index) + ".cso";
}

std::string Dx12ShaderTool::MakeShaderDisassemblyFileName(uint64_t handle_id, ShaderType type)
{
    std::string suffix;
    switch (type)
    {
        case ShaderType::kVertex:
            suffix = ".vs.txt";
            break;
        case ShaderType::kPixel:
            suffix = ".ps.txt";
            break;
        case ShaderType::kDomain:
            suffix = ".ds.txt";
            break;
        case ShaderType::kHull:
            suffix = ".hs.txt";
            break;
        case ShaderType::kGeometry:
            suffix = ".gs.txt";
            break;
        case ShaderType::kCompute:
            suffix = ".cs.txt";
            break;
        default:
            suffix = ".txt";
            break;
    }

    return "sh" + std::to_string(handle_id) + suffix;
}

std::string Dx12ShaderTool::MakeStateObjectDxilLibraryDisassemblyFileName(uint64_t handle_id, uint32_t subobject_index)
{
    return "sh" + std::to_string(handle_id) + "_" + std::to_string(subobject_index) + ".txt";
}

std::string Dx12ShaderTool::MakeRootSignatureFileName(uint64_t handle_id)
{
    return "rs" + std::to_string(handle_id) + ".rootsig";
}

std::string Dx12ShaderTool::MakeRootSignatureReserializedFileName(uint64_t handle_id)
{
    return "rs" + std::to_string(handle_id) + "_reserialized.rootsig";
}

std::string Dx12ShaderTool::MakeRootSignatureTextFileName(uint64_t handle_id)
{
    return "rs" + std::to_string(handle_id) + ".rootsig.txt";
}

static bool WriteTextFileToDir(const std::string& extract_dir, const std::string& file_name, const std::string& text)
{
    std::string file_path = util::filepath::Join(extract_dir, file_name);

    FILE* fp     = nullptr;
    auto  result = util::platform::FileOpen(&fp, file_path.c_str(), "wb");
    if ((result == 0) && (fp != nullptr))
    {
        if (!text.empty())
        {
            if (!util::platform::FileWrite(text.data(), text.size(), fp))
            {
                GFXRECON_WRITE_CONSOLE("Error while writing file %s: Could not complete", file_name.c_str());
                util::platform::FileClose(fp);
                return false;
            }
        }

        util::platform::FileClose(fp);
        return true;
    }

    GFXRECON_WRITE_CONSOLE("Error while writing file %s: Could not open", file_name.c_str());
    return false;
}

#if defined(WIN32)
static const char* RootSignatureVersionToString(D3D_ROOT_SIGNATURE_VERSION version)
{
    switch (version)
    {
        case D3D_ROOT_SIGNATURE_VERSION_1_0:
            return "1_0";
        case D3D_ROOT_SIGNATURE_VERSION_1_1:
            return "1_1";
#ifdef D3D_ROOT_SIGNATURE_VERSION_1_2
        case D3D_ROOT_SIGNATURE_VERSION_1_2:
            return "1_2";
#endif
        default:
            return "unknown";
    }
}

static const char* RootParameterTypeToString(D3D12_ROOT_PARAMETER_TYPE type)
{
    switch (type)
    {
        case D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE:
            return "descriptor_table";
        case D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS:
            return "32bit_constants";
        case D3D12_ROOT_PARAMETER_TYPE_CBV:
            return "cbv";
        case D3D12_ROOT_PARAMETER_TYPE_SRV:
            return "srv";
        case D3D12_ROOT_PARAMETER_TYPE_UAV:
            return "uav";
        default:
            return "unknown";
    }
}

static const char* ShaderVisibilityToString(D3D12_SHADER_VISIBILITY vis)
{
    switch (vis)
    {
        case D3D12_SHADER_VISIBILITY_ALL:
            return "all";
        case D3D12_SHADER_VISIBILITY_VERTEX:
            return "vertex";
        case D3D12_SHADER_VISIBILITY_HULL:
            return "hull";
        case D3D12_SHADER_VISIBILITY_DOMAIN:
            return "domain";
        case D3D12_SHADER_VISIBILITY_GEOMETRY:
            return "geometry";
        case D3D12_SHADER_VISIBILITY_PIXEL:
            return "pixel";
        case D3D12_SHADER_VISIBILITY_AMPLIFICATION:
            return "amplification";
        case D3D12_SHADER_VISIBILITY_MESH:
            return "mesh";
        default:
            return "unknown";
    }
}

static const char* DescriptorRangeTypeToString(D3D12_DESCRIPTOR_RANGE_TYPE type)
{
    switch (type)
    {
        case D3D12_DESCRIPTOR_RANGE_TYPE_SRV:
            return "srv";
        case D3D12_DESCRIPTOR_RANGE_TYPE_UAV:
            return "uav";
        case D3D12_DESCRIPTOR_RANGE_TYPE_CBV:
            return "cbv";
        case D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER:
            return "sampler";
        default:
            return "unknown";
    }
}

static void AppendFlags(std::string& out, D3D12_ROOT_SIGNATURE_FLAGS flags)
{
    // Always write the numeric value to keep this stable across SDKs.
    out += "Flags=0x" + std::to_string(static_cast<uint32_t>(flags)) + "\n";
}

static void AppendDesc_1_0(std::string& out, const D3D12_ROOT_SIGNATURE_DESC& desc)
{
    AppendFlags(out, desc.Flags);
    out += "NumParameters=" + std::to_string(desc.NumParameters) + "\n";
    out += "NumStaticSamplers=" + std::to_string(desc.NumStaticSamplers) + "\n";

    for (UINT i = 0; i < desc.NumParameters; ++i)
    {
        const auto& p = desc.pParameters[i];
        out += "Parameter[" + std::to_string(i) + "]: type=" + RootParameterTypeToString(p.ParameterType) +
               ", visibility=" + ShaderVisibilityToString(p.ShaderVisibility) + "\n";

        if (p.ParameterType == D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE)
        {
            out += "  DescriptorTable.NumRanges=" + std::to_string(p.DescriptorTable.NumDescriptorRanges) + "\n";
            for (UINT r = 0; r < p.DescriptorTable.NumDescriptorRanges; ++r)
            {
                const auto& range = p.DescriptorTable.pDescriptorRanges[r];
                out += "  Range[" + std::to_string(r) + "]: type=" + DescriptorRangeTypeToString(range.RangeType) +
                       ", num=" + std::to_string(range.NumDescriptors) +
                       ", base_reg=" + std::to_string(range.BaseShaderRegister) +
                       ", space=" + std::to_string(range.RegisterSpace) +
                       ", offset=" + std::to_string(range.OffsetInDescriptorsFromTableStart) + "\n";
            }
        }
        else if (p.ParameterType == D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS)
        {
            out += "  Constants: num32=" + std::to_string(p.Constants.Num32BitValues) +
                   ", reg=" + std::to_string(p.Constants.ShaderRegister) +
                   ", space=" + std::to_string(p.Constants.RegisterSpace) + "\n";
        }
        else
        {
            out += "  Descriptor: reg=" + std::to_string(p.Descriptor.ShaderRegister) +
                   ", space=" + std::to_string(p.Descriptor.RegisterSpace) + "\n";
        }
    }
}

static void AppendDesc_1_1(std::string& out, const D3D12_ROOT_SIGNATURE_DESC1& desc)
{
    AppendFlags(out, desc.Flags);
    out += "NumParameters=" + std::to_string(desc.NumParameters) + "\n";
    out += "NumStaticSamplers=" + std::to_string(desc.NumStaticSamplers) + "\n";

    for (UINT i = 0; i < desc.NumParameters; ++i)
    {
        const auto& p = desc.pParameters[i];
        out += "Parameter[" + std::to_string(i) + "]: type=" + RootParameterTypeToString(p.ParameterType) +
               ", visibility=" + ShaderVisibilityToString(p.ShaderVisibility) + "\n";

        if (p.ParameterType == D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE)
        {
            out += "  DescriptorTable.NumRanges=" + std::to_string(p.DescriptorTable.NumDescriptorRanges) + "\n";
            for (UINT r = 0; r < p.DescriptorTable.NumDescriptorRanges; ++r)
            {
                const auto& range = p.DescriptorTable.pDescriptorRanges[r];
                out += "  Range[" + std::to_string(r) + "]: type=" + DescriptorRangeTypeToString(range.RangeType) +
                       ", num=" + std::to_string(range.NumDescriptors) +
                       ", base_reg=" + std::to_string(range.BaseShaderRegister) +
                       ", space=" + std::to_string(range.RegisterSpace) + ", flags=0x" +
                       std::to_string(static_cast<uint32_t>(range.Flags)) +
                       ", offset=" + std::to_string(range.OffsetInDescriptorsFromTableStart) + "\n";
            }
        }
        else if (p.ParameterType == D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS)
        {
            out += "  Constants: num32=" + std::to_string(p.Constants.Num32BitValues) +
                   ", reg=" + std::to_string(p.Constants.ShaderRegister) +
                   ", space=" + std::to_string(p.Constants.RegisterSpace) + "\n";
        }
        else
        {
            out += "  Descriptor: reg=" + std::to_string(p.Descriptor.ShaderRegister) +
                   ", space=" + std::to_string(p.Descriptor.RegisterSpace) + ", flags=0x" +
                   std::to_string(static_cast<uint32_t>(p.Descriptor.Flags)) + "\n";
        }
    }
}

#ifdef D3D_ROOT_SIGNATURE_VERSION_1_2
static void AppendDesc_1_2(std::string& out, const D3D12_ROOT_SIGNATURE_DESC2& desc)
{
    AppendFlags(out, desc.Flags);
    out += "NumParameters=" + std::to_string(desc.NumParameters) + "\n";
    out += "NumStaticSamplers=" + std::to_string(desc.NumStaticSamplers) + "\n";

    for (UINT i = 0; i < desc.NumParameters; ++i)
    {
        const auto& p = desc.pParameters[i];
        out += "Parameter[" + std::to_string(i) + "]: type=" + RootParameterTypeToString(p.ParameterType) +
               ", visibility=" + ShaderVisibilityToString(p.ShaderVisibility) + "\n";

        if (p.ParameterType == D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE)
        {
            out += "  DescriptorTable.NumRanges=" + std::to_string(p.DescriptorTable.NumDescriptorRanges) + "\n";
            for (UINT r = 0; r < p.DescriptorTable.NumDescriptorRanges; ++r)
            {
                const auto& range = p.DescriptorTable.pDescriptorRanges[r];
                out += "  Range[" + std::to_string(r) + "]: type=" + DescriptorRangeTypeToString(range.RangeType) +
                       ", num=" + std::to_string(range.NumDescriptors) +
                       ", base_reg=" + std::to_string(range.BaseShaderRegister) +
                       ", space=" + std::to_string(range.RegisterSpace) + ", flags=0x" +
                       std::to_string(static_cast<uint32_t>(range.Flags)) +
                       ", offset=" + std::to_string(range.OffsetInDescriptorsFromTableStart) + "\n";
            }
        }
        else if (p.ParameterType == D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS)
        {
            out += "  Constants: num32=" + std::to_string(p.Constants.Num32BitValues) +
                   ", reg=" + std::to_string(p.Constants.ShaderRegister) +
                   ", space=" + std::to_string(p.Constants.RegisterSpace) + "\n";
        }
        else
        {
            out += "  Descriptor: reg=" + std::to_string(p.Descriptor.ShaderRegister) +
                   ", space=" + std::to_string(p.Descriptor.RegisterSpace) + ", flags=0x" +
                   std::to_string(static_cast<uint32_t>(p.Descriptor.Flags)) + "\n";
        }
    }
}
#endif

static bool BuildRootSignatureText(const void* blob, size_t blob_size, std::string& out_text)
{
    using Microsoft::WRL::ComPtr;

    out_text.clear();
    if ((blob == nullptr) || (blob_size == 0))
    {
        return false;
    }

    ComPtr<ID3D12VersionedRootSignatureDeserializer> versioned;
    HRESULT                                          hr = D3D12CreateVersionedRootSignatureDeserializer(
        blob, blob_size, IID_PPV_ARGS(versioned.ReleaseAndGetAddressOf()));
    if (SUCCEEDED(hr) && versioned)
    {
        const D3D12_VERSIONED_ROOT_SIGNATURE_DESC* desc = nullptr;

#ifdef D3D_ROOT_SIGNATURE_VERSION_1_2
        D3D_ROOT_SIGNATURE_VERSION target_version = D3D_ROOT_SIGNATURE_VERSION_1_2;
#else
        D3D_ROOT_SIGNATURE_VERSION target_version = D3D_ROOT_SIGNATURE_VERSION_1_1;
#endif
        hr = versioned->GetRootSignatureDescAtVersion(target_version, &desc);
        if (FAILED(hr))
        {
            target_version = D3D_ROOT_SIGNATURE_VERSION_1_0;
            hr             = versioned->GetRootSignatureDescAtVersion(target_version, &desc);
        }

        if (SUCCEEDED(hr) && (desc != nullptr))
        {
            out_text += "RootSignatureDescVersion=";
            out_text += RootSignatureVersionToString(desc->Version);
            out_text += "\n";

            if (desc->Version == D3D_ROOT_SIGNATURE_VERSION_1_0)
            {
                AppendDesc_1_0(out_text, desc->Desc_1_0);
            }
            else if (desc->Version == D3D_ROOT_SIGNATURE_VERSION_1_1)
            {
                AppendDesc_1_1(out_text, desc->Desc_1_1);
            }
#ifdef D3D_ROOT_SIGNATURE_VERSION_1_2
            else if (desc->Version == D3D_ROOT_SIGNATURE_VERSION_1_2)
            {
                AppendDesc_1_2(out_text, desc->Desc_1_2);
            }
#endif
            else
            {
                out_text += "Unsupported root signature version\n";
            }
            return true;
        }
    }

    // Fallback: non-versioned deserializer (1.0).
    ComPtr<ID3D12RootSignatureDeserializer> deserializer;
    hr = D3D12CreateRootSignatureDeserializer(blob, blob_size, IID_PPV_ARGS(deserializer.ReleaseAndGetAddressOf()));
    if (SUCCEEDED(hr) && deserializer)
    {
        auto desc = deserializer->GetRootSignatureDesc();
        if (desc != nullptr)
        {
            out_text += "RootSignatureDescVersion=1_0\n";
            AppendDesc_1_0(out_text, *desc);
            return true;
        }
    }

    return false;
}

static bool
TryReserializeRootSignature(const void* blob, size_t blob_size, std::unique_ptr<char[]>& out_data, size_t& out_size)
{
    using Microsoft::WRL::ComPtr;

    out_data.reset();
    out_size = 0;

    if ((blob == nullptr) || (blob_size == 0))
    {
        return false;
    }

    ComPtr<ID3D12VersionedRootSignatureDeserializer> versioned;
    HRESULT                                          hr = D3D12CreateVersionedRootSignatureDeserializer(
        blob, blob_size, IID_PPV_ARGS(versioned.ReleaseAndGetAddressOf()));
    if (SUCCEEDED(hr) && versioned)
    {
        const D3D12_VERSIONED_ROOT_SIGNATURE_DESC* desc = nullptr;

#ifdef D3D_ROOT_SIGNATURE_VERSION_1_2
        D3D_ROOT_SIGNATURE_VERSION target_version = D3D_ROOT_SIGNATURE_VERSION_1_2;
#else
        D3D_ROOT_SIGNATURE_VERSION target_version = D3D_ROOT_SIGNATURE_VERSION_1_1;
#endif
        hr = versioned->GetRootSignatureDescAtVersion(target_version, &desc);
        if (FAILED(hr))
        {
            target_version = D3D_ROOT_SIGNATURE_VERSION_1_0;
            hr             = versioned->GetRootSignatureDescAtVersion(target_version, &desc);
        }

        if (SUCCEEDED(hr) && (desc != nullptr))
        {
            ComPtr<ID3DBlob> blob_out;
            ComPtr<ID3DBlob> error_blob;
            hr = D3D12SerializeVersionedRootSignature(
                desc, blob_out.ReleaseAndGetAddressOf(), error_blob.ReleaseAndGetAddressOf());
            if (FAILED(hr) || !blob_out)
            {
                if (error_blob && error_blob->GetBufferPointer() && (error_blob->GetBufferSize() > 0))
                {
                    GFXRECON_LOG_WARNING("Failed to reserialize root signature: %s",
                                         static_cast<const char*>(error_blob->GetBufferPointer()));
                }
                return false;
            }

            out_size = static_cast<size_t>(blob_out->GetBufferSize());
            out_data = std::make_unique<char[]>(out_size);
            std::memcpy(out_data.get(), blob_out->GetBufferPointer(), out_size);
            return true;
        }
    }

    // Fallback: root signature 1.0
    ComPtr<ID3D12RootSignatureDeserializer> deserializer;
    hr = D3D12CreateRootSignatureDeserializer(blob, blob_size, IID_PPV_ARGS(deserializer.ReleaseAndGetAddressOf()));
    if (SUCCEEDED(hr) && deserializer)
    {
        auto desc = deserializer->GetRootSignatureDesc();
        if (desc != nullptr)
        {
            ComPtr<ID3DBlob> blob_out;
            ComPtr<ID3DBlob> error_blob;
            hr = D3D12SerializeRootSignature(desc,
                                             D3D_ROOT_SIGNATURE_VERSION_1_0,
                                             blob_out.ReleaseAndGetAddressOf(),
                                             error_blob.ReleaseAndGetAddressOf());
            if (FAILED(hr) || !blob_out)
            {
                if (error_blob && error_blob->GetBufferPointer() && (error_blob->GetBufferSize() > 0))
                {
                    GFXRECON_LOG_WARNING("Failed to reserialize root signature: %s",
                                         static_cast<const char*>(error_blob->GetBufferPointer()));
                }
                return false;
            }

            out_size = static_cast<size_t>(blob_out->GetBufferSize());
            out_data = std::make_unique<char[]>(out_size);
            std::memcpy(out_data.get(), blob_out->GetBufferPointer(), out_size);
            return true;
        }
    }

    return false;
}
#endif

// Internal helper: disassemble shader bytecode to human-readable text.
// Tries DXC first (for DXIL SM6.0+), then falls back to D3DDisassemble (for DXBC SM5.x).
static bool DisassembleShaderBytecode(const void* code, size_t code_size, std::string& out_text)
{
    out_text.clear();
    if ((code == nullptr) || (code_size == 0))
    {
        return false;
    }

#if defined(GFXRECON_DXC_SUPPORT)
    {
        using Microsoft::WRL::ComPtr;

        ComPtr<IDxcUtils> dxc_utils;
        HRESULT           hr = DxcCreateInstance(CLSID_DxcUtils, IID_PPV_ARGS(&dxc_utils));
        if (SUCCEEDED(hr) && dxc_utils)
        {
            ComPtr<IDxcBlobEncoding> blob_encoding;
            hr = dxc_utils->CreateBlobFromPinned(code, static_cast<UINT32>(code_size), DXC_CP_ACP, &blob_encoding);
            if (SUCCEEDED(hr) && blob_encoding)
            {
                ComPtr<IDxcCompiler> compiler;
                hr = DxcCreateInstance(CLSID_DxcCompiler, IID_PPV_ARGS(&compiler));
                if (SUCCEEDED(hr) && compiler)
                {
                    ComPtr<IDxcBlobEncoding> disassembly;
                    hr = compiler->Disassemble(blob_encoding.Get(), &disassembly);
                    if (SUCCEEDED(hr) && disassembly && (disassembly->GetBufferSize() > 0))
                    {
                        out_text.assign(static_cast<const char*>(disassembly->GetBufferPointer()),
                                        disassembly->GetBufferSize());
                        // Trim trailing null if present
                        while (!out_text.empty() && out_text.back() == '\0')
                        {
                            out_text.pop_back();
                        }
                        return true;
                    }
                }
            }
        }
    }
#endif

#if defined(WIN32)
    // Fallback: D3DDisassemble for DXBC (SM5.x and below)
    {
        using Microsoft::WRL::ComPtr;

        ComPtr<ID3DBlob> disassembly;
        HRESULT          hr = D3DDisassemble(code, code_size, 0, nullptr, &disassembly);
        if (SUCCEEDED(hr) && disassembly && (disassembly->GetBufferSize() > 0))
        {
            out_text.assign(static_cast<const char*>(disassembly->GetBufferPointer()), disassembly->GetBufferSize());
            // Trim trailing null if present
            while (!out_text.empty() && out_text.back() == '\0')
            {
                out_text.pop_back();
            }
            return true;
        }
    }
#endif

    return false;
}

bool Dx12ShaderTool::ExtractShaderToDir(const std::string& extract_dir,
                                        const std::string& file_name,
                                        const void*        code,
                                        size_t             code_size)
{
    if ((code == nullptr) || (code_size == 0))
    {
        return false;
    }

    std::string file_path = util::filepath::Join(extract_dir, file_name);

    FILE* fp     = nullptr;
    auto  result = util::platform::FileOpen(&fp, file_path.c_str(), "wb");
    if ((result == 0) && (fp != nullptr))
    {
        if (!util::platform::FileWrite(code, code_size, fp))
        {
            GFXRECON_WRITE_CONSOLE("Error while writing file %s: Could not complete", file_name.c_str());
            util::platform::FileClose(fp);
            return false;
        }
        util::platform::FileClose(fp);
        return true;
    }

    GFXRECON_WRITE_CONSOLE("Error while writing file %s: Could not open", file_name.c_str());
    return false;
}

bool Dx12ShaderTool::ExtractPipelineShaderToDir(
    const std::string& extract_dir, uint64_t handle_id, ShaderType type, const void* code, size_t code_size)
{
    return ExtractShaderToDir(extract_dir, MakePipelineShaderFileName(handle_id, type), code, code_size);
}

bool Dx12ShaderTool::ExtractStateObjectDxilLibraryToDir(const std::string& extract_dir,
                                                        uint64_t           state_object_handle_id,
                                                        uint32_t           subobject_index,
                                                        const void*        code,
                                                        size_t             code_size)
{
    return ExtractShaderToDir(
        extract_dir, MakeStateObjectDxilLibraryFileName(state_object_handle_id, subobject_index), code, code_size);
}

bool Dx12ShaderTool::ExtractRootSignatureTextToDir(const std::string& extract_dir,
                                                   uint64_t           handle_id,
                                                   const void*        blob,
                                                   size_t             blob_size)
{
#if defined(WIN32)
    std::string text;
    if (BuildRootSignatureText(blob, blob_size, text))
    {
        return WriteTextFileToDir(extract_dir, MakeRootSignatureTextFileName(handle_id), text);
    }
    return false;
#else
    (void)extract_dir;
    (void)handle_id;
    (void)blob;
    (void)blob_size;
    return false;
#endif
}

bool Dx12ShaderTool::ExtractRootSignatureBinaryToDir(const std::string& extract_dir,
                                                     uint64_t           handle_id,
                                                     const void*        blob,
                                                     size_t             blob_size)
{
    bool wrote_original = ExtractShaderToDir(extract_dir, MakeRootSignatureFileName(handle_id), blob, blob_size);
    if (!wrote_original)
    {
        return false;
    }

#if defined(WIN32)
    // Best-effort: also emit a reserialized blob to make diffs easier.
    std::unique_ptr<char[]> reser;
    size_t                  reser_size = 0;
    if (TryReserializeRootSignature(blob, blob_size, reser, reser_size))
    {
        ExtractShaderToDir(extract_dir, MakeRootSignatureReserializedFileName(handle_id), reser.get(), reser_size);
    }
#endif

    return true;
}

bool Dx12ShaderTool::ExtractRootSignatureToDir(const std::string& extract_dir,
                                               uint64_t           handle_id,
                                               const void*        blob,
                                               size_t             blob_size)
{
    bool ok = ExtractRootSignatureBinaryToDir(extract_dir, handle_id, blob, blob_size);
    ExtractRootSignatureTextToDir(extract_dir, handle_id, blob, blob_size);
    return ok;
}

bool Dx12ShaderTool::DisassemblePipelineShaderToDir(
    const std::string& extract_dir, uint64_t handle_id, ShaderType type, const void* code, size_t code_size)
{
    std::string disasm_text;
    if (!DisassembleShaderBytecode(code, code_size, disasm_text))
    {
        GFXRECON_LOG_WARNING(
            "Failed to disassemble pipeline shader sh%" PRIu64 ".%s", handle_id, ShaderTypeToString(type));
        return false;
    }

    return WriteTextFileToDir(extract_dir, MakeShaderDisassemblyFileName(handle_id, type), disasm_text);
}

bool Dx12ShaderTool::DisassembleStateObjectDxilLibraryToDir(const std::string& extract_dir,
                                                            uint64_t           state_object_handle_id,
                                                            uint32_t           subobject_index,
                                                            const void*        code,
                                                            size_t             code_size)
{
    std::string disasm_text;
    if (!DisassembleShaderBytecode(code, code_size, disasm_text))
    {
        GFXRECON_LOG_WARNING(
            "Failed to disassemble state object sh%" PRIu64 " subobject %u", state_object_handle_id, subobject_index);
        return false;
    }

    return WriteTextFileToDir(extract_dir,
                              MakeStateObjectDxilLibraryDisassemblyFileName(state_object_handle_id, subobject_index),
                              disasm_text);
}

bool Dx12ShaderTool::LoadReplacementShaderFromDir(const std::string&       replace_shader_dir,
                                                  const std::string&       file_name,
                                                  std::unique_ptr<char[]>& out_code,
                                                  size_t&                  out_size)
{
    out_code.reset();
    out_size = 0;

    if (replace_shader_dir.empty())
    {
        return false;
    }

    std::string file_path = util::filepath::Join(replace_shader_dir, file_name);
    FILE*       fp        = nullptr;
    int32_t     result    = util::platform::FileOpen(&fp, file_path.c_str(), "rb");
    if ((result != 0) || (fp == nullptr))
    {
        return false;
    }

    util::platform::FileSeek(fp, 0L, util::platform::FileSeekEnd);
    size_t file_size = static_cast<size_t>(util::platform::FileTell(fp));
    util::platform::FileSeek(fp, 0L, util::platform::FileSeekSet);
    if (file_size == 0)
    {
        util::platform::FileClose(fp);
        return false;
    }

    auto buffer = std::make_unique<char[]>(file_size);
    util::platform::FileRead(buffer.get(), file_size, fp);
    util::platform::FileClose(fp);

    out_code = std::move(buffer);
    out_size = file_size;
    return true;
}

bool Dx12ShaderTool::LoadReplacementPipelineShaderFromDir(const std::string&       replace_shader_dir,
                                                          uint64_t                 handle_id,
                                                          ShaderType               type,
                                                          std::unique_ptr<char[]>& out_code,
                                                          size_t&                  out_size)
{
    return LoadReplacementShaderFromDir(
        replace_shader_dir, MakePipelineShaderFileName(handle_id, type), out_code, out_size);
}

bool Dx12ShaderTool::LoadReplacementStateObjectDxilLibraryFromDir(const std::string&       replace_shader_dir,
                                                                  uint64_t                 handle_id,
                                                                  uint32_t                 subobject_index,
                                                                  std::unique_ptr<char[]>& out_code,
                                                                  size_t&                  out_size)
{
    return LoadReplacementShaderFromDir(
        replace_shader_dir, MakeStateObjectDxilLibraryFileName(handle_id, subobject_index), out_code, out_size);
}

GFXRECON_END_NAMESPACE(graphics)
GFXRECON_END_NAMESPACE(gfxrecon)