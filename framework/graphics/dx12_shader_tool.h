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

#ifndef GFXRECON_GRAPHICS_SHADER_TOOL_H
#define GFXRECON_GRAPHICS_SHADER_TOOL_H

#include "util/defines.h"
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>

GFXRECON_BEGIN_NAMESPACE(gfxrecon)
GFXRECON_BEGIN_NAMESPACE(graphics)

class Dx12ShaderTool
{
  public:
    enum class ShaderType
    {
        kVertex,
        kDomain,
        kHull,
        kGeometry,
        kPixel,
        kCompute,
        // StateObject subobject type D3D12_STATE_SUBOBJECT_TYPE_DXIL_LIBRARY
        kStateObjectDxilLibrary,
        kUnknown
    };

    static const char* ShaderTypeToString(ShaderType type);

    // File naming convention matches tools/extract and replay --replace-shaders logic.
    // Graphics/compute pipeline stage shaders:
    //   sh<handle_id>.<stage>.cso
    // StateObject DXIL libraries:
    //   sh<handle_id>_<subobject_index>.cso
    // Root signatures:
    //   rs<handle_id>.rootsig              (original serialized blob)
    //   rs<handle_id>_reserialized.rootsig (deserialize + serialize result)
    //   rs<handle_id>.rootsig.txt          (human-readable description)
    static std::string MakePipelineShaderFileName(uint64_t handle_id, ShaderType type);
    static std::string MakeStateObjectDxilLibraryFileName(uint64_t handle_id, uint32_t subobject_index);
    static std::string MakeRootSignatureFileName(uint64_t handle_id);
    static std::string MakeRootSignatureReserializedFileName(uint64_t handle_id);
    static std::string MakeRootSignatureTextFileName(uint64_t handle_id);

    static bool ExtractShaderToDir(const std::string& extract_dir,
                                   const std::string& file_name,
                                   const void*        code,
                                   size_t             code_size);

    static bool ExtractPipelineShaderToDir(
        const std::string& extract_dir, uint64_t handle_id, ShaderType type, const void* code, size_t code_size);

    static bool ExtractStateObjectDxilLibraryToDir(const std::string& extract_dir,
                                                   uint64_t           state_object_handle_id,
                                                   uint32_t           subobject_index,
                                                   const void*        code,
                                                   size_t             code_size);

    // Writes the human-readable text description (.rootsig.txt). Default extraction output.
    static bool ExtractRootSignatureTextToDir(const std::string& extract_dir,
                                              uint64_t           handle_id,
                                              const void*        blob,
                                              size_t             blob_size);

    // Writes the serialized binary blobs (.rootsig, _reserialized.rootsig). Opt-in output.
    static bool ExtractRootSignatureBinaryToDir(const std::string& extract_dir,
                                                uint64_t           handle_id,
                                                const void*        blob,
                                                size_t             blob_size);

    // Writes everything (text + binary). Retained for callers that want the full set.
    static bool
    ExtractRootSignatureToDir(const std::string& extract_dir, uint64_t handle_id, const void* blob, size_t blob_size);

    // Human-readable disassembly output file names:
    //   sh<handle_id>.<stage>.txt   (pipeline shader disassembly)
    //   sh<handle_id>_<subobject_index>.txt (state object DXIL library disassembly)
    static std::string MakeShaderDisassemblyFileName(uint64_t handle_id, ShaderType type);
    static std::string MakeStateObjectDxilLibraryDisassemblyFileName(uint64_t handle_id, uint32_t subobject_index);

    // Disassemble shader bytecode and write human-readable text alongside the .cso binary.
    // Uses DXC IDxcCompiler::Disassemble for DXIL (SM6.0+) and D3DDisassemble for DXBC (SM5.x).
    // Returns true if a .txt file was successfully written.
    static bool DisassemblePipelineShaderToDir(
        const std::string& extract_dir, uint64_t handle_id, ShaderType type, const void* code, size_t code_size);

    static bool DisassembleStateObjectDxilLibraryToDir(const std::string& extract_dir,
                                                       uint64_t           state_object_handle_id,
                                                       uint32_t           subobject_index,
                                                       const void*        code,
                                                       size_t             code_size);

    // Replacement helpers (read shader bytecode from replace_shader_dir)
    static bool LoadReplacementShaderFromDir(const std::string&       replace_shader_dir,
                                             const std::string&       file_name,
                                             std::unique_ptr<char[]>& out_code,
                                             size_t&                  out_size);

    static bool LoadReplacementPipelineShaderFromDir(const std::string&       replace_shader_dir,
                                                     uint64_t                 handle_id,
                                                     ShaderType               type,
                                                     std::unique_ptr<char[]>& out_code,
                                                     size_t&                  out_size);

    static bool LoadReplacementStateObjectDxilLibraryFromDir(const std::string&       replace_shader_dir,
                                                             uint64_t                 handle_id,
                                                             uint32_t                 subobject_index,
                                                             std::unique_ptr<char[]>& out_code,
                                                             size_t&                  out_size);
};

GFXRECON_END_NAMESPACE(graphics)
GFXRECON_END_NAMESPACE(gfxrecon)

#endif // GFXRECON_GRAPHICS_SHADER_TOOL_H
