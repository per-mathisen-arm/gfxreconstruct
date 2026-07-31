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

#include PROJECT_VERSION_HEADER_FILE

#include "../optimize/vulkan_file_optimizer.h"
#include "../optimize/vulkan_spirv_tracker_modifier.h"
#include "../tool_settings.h"
#include "../tool_command_line.h"

#if defined(D3D12_SUPPORT)
#include "../optimize/dx12_optimize_util.h"
#endif

#include "decode/decode_api_detection.h"
#include "decode/file_processor.h"
#include "format/format.h"
#include "format/format_util.h"
#include "generated/generated_vulkan_decoder.h"
#include "util/argument_parser.h"
#include "util/logging.h"
#include "util/date_time.h"

#include "vulkan/vulkan.h"

#include <filesystem>
#include <cassert>
#include <stdexcept>
#include <string>
#include <unordered_set>
#include <vector>

const char kVerboseOption[]            = "--verbose";
const char kErrorOnBuffersIncomplete[] = "--error-on-buffers-incomplete";
const char kWriteFixupJson[]           = "--write-fixup-json";

const char kOptions[]   = "-h|--help,--version,--verbose,--error-on-buffers-incomplete";
const char kArguments[] = "--gpu,--set-replay-options,--set-replay-options,--write-fixup-json";

static void PrintUsage(const char* exe_name)
{
    std::string app_name     = exe_name;
    size_t      dir_location = app_name.find_last_of("/\\");
    if (dir_location >= 0)
    {
        app_name.replace(0, dir_location + 1, "");
    }
    GFXRECON_WRITE_CONSOLE("\n%s - Run spirv tracker analysis and optionally generate a modified trace.",
                           app_name.c_str());

    GFXRECON_WRITE_CONSOLE("");
    GFXRECON_WRITE_CONSOLE("Usage:");
    GFXRECON_WRITE_CONSOLE("  %s [-h | --help] [--version] [--verbose] [--error-on-buffers-incomplete] "
                           "[--write-fixup-json <path>] <input-file> [output-file]",
                           app_name.c_str());
    GFXRECON_WRITE_CONSOLE("");
    GFXRECON_WRITE_CONSOLE("Required arguments:");
    GFXRECON_WRITE_CONSOLE("  <input-file>\t\tThe path to input GFXReconstruct capture file to be processed.");
    GFXRECON_WRITE_CONSOLE("");
    GFXRECON_WRITE_CONSOLE("Optional arguments:");
    GFXRECON_WRITE_CONSOLE("  [output-file]\t\tIf provided, run the modification pass and write a modified trace.");
    GFXRECON_WRITE_CONSOLE("  --write-fixup-json <path>\tWrite fixup locations to JSON.");
    GFXRECON_WRITE_CONSOLE("  	\t\tValid file values: example.json, ./example.json, /workdir/example.json");
    GFXRECON_WRITE_CONSOLE("  	\t\tValid directory values: ., ./, /workdir, /workdir/");
    GFXRECON_WRITE_CONSOLE(
        "  	\t\tWhen <path> resolves to a directory, the output file defaults to <input-stem>-fixup.json.");
    GFXRECON_WRITE_CONSOLE("");
}

namespace
{

bool EndsWithPathSeparator(const std::string& value)
{
    return !value.empty() && ((value.back() == '/') || (value.back() == '\\'));
}

std::filesystem::path MakeDefaultFixupJsonFilename(const std::string& input_filename)
{
    const std::filesystem::path input_path(input_filename);
    std::string                 stem = input_path.stem().string();
    if (stem.empty())
    {
        stem = input_path.filename().string();
    }
    if (stem.empty())
    {
        stem = "trace";
    }
    return std::filesystem::path(stem + "-fixup.json");
}

void CreateDirectoryIfNeeded(const std::filesystem::path& dir_path)
{
    std::error_code ec;
    if (std::filesystem::exists(dir_path, ec))
    {
        if (ec)
        {
            throw std::runtime_error("Failed to inspect fixup JSON directory: " + dir_path.string());
        }
        if (!std::filesystem::is_directory(dir_path, ec) || ec)
        {
            throw std::runtime_error("Fixup JSON path is not a directory: " + dir_path.string());
        }
        return;
    }

    if (!std::filesystem::create_directories(dir_path, ec) && ec)
    {
        throw std::runtime_error("Failed to create fixup JSON directory: " + dir_path.string());
    }
}

std::optional<std::filesystem::path> ResolveFixupJsonPath(const std::string&                    input_filename,
                                                          const gfxrecon::util::ArgumentParser& arg_parser)
{
    if (!arg_parser.IsArgumentSet(kWriteFixupJson))
    {
        return std::nullopt;
    }

    const std::string raw_path = arg_parser.GetArgumentValue(kWriteFixupJson);
    if (raw_path.empty())
    {
        throw std::runtime_error("--write-fixup-json requires a non-empty path");
    }

    const std::filesystem::path requested_path(raw_path);
    const std::filesystem::path default_filename = MakeDefaultFixupJsonFilename(input_filename);
    std::error_code             ec;

    // end with "/", eg "/workdir/test/"
    if (EndsWithPathSeparator(raw_path))
    {
        CreateDirectoryIfNeeded(requested_path);
        return requested_path / default_filename;
    }

    // existed dir without ending "/", eg "/workdir/test"
    if (std::filesystem::exists(requested_path, ec))
    {
        if (ec)
        {
            throw std::runtime_error("Failed to inspect fixup JSON path: " + requested_path.string());
        }
        if (std::filesystem::is_directory(requested_path, ec))
        {
            if (ec)
            {
                throw std::runtime_error("Failed to inspect fixup JSON directory: " + requested_path.string());
            }
            return requested_path / default_filename;
        }
    }

    if (requested_path.extension() != ".json")
    {
        throw std::runtime_error("--write-fixup-json must be a .json file path or an existing directory path");
    }

    const std::filesystem::path parent_path = requested_path.parent_path();
    if (!parent_path.empty())
    {
        CreateDirectoryIfNeeded(parent_path);
    }
    return requested_path;
}

} // namespace

void RunSpirvTracker(const std::string&                                 input_filename,
                     const std::string&                                 output_filename,
                     const gfxrecon::decode::VulkanSpirvTrackerOptions& options)
{
    gfxrecon::decode::FileProcessor file_processor;
    if (file_processor.Initialize(input_filename))
    {
        gfxrecon::decode::VulkanDecoder decoder;
        auto spirv_tracker_modifier_consumer = std::make_unique<gfxrecon::decode::VulkanSpirvTrackModifier>(options);

        decoder.AddConsumer(spirv_tracker_modifier_consumer.get());

        file_processor.AddDecoder(&decoder);
        file_processor.ProcessAllFrames();

        if (file_processor.GetErrorState() != gfxrecon::decode::kErrorNone)
        {
            throw std::runtime_error("Failed to scan input file for optimizations");
        }

        // Finalize the analysis pass before deciding whether the modification pass
        // needs to emit rewritten trace metadata.
        spirv_tracker_modifier_consumer->FinalizeAnalysis();

        if (output_filename.empty())
        {
            return;
        }

        if (!spirv_tracker_modifier_consumer->CanOptimize())
        {
            GFXRECON_WRITE_CONSOLE("Nothing to optimize. Exiting.");
            return;
        }

        spirv_tracker_modifier_consumer->PrepareForModificationPass();

        gfxrecon::VulkanFileOptimizer::VulkanOptimizationData optimization_data;
        optimization_data.modifiers.push_back(std::move(spirv_tracker_modifier_consumer));

        gfxrecon::VulkanFileOptimizer file_optimizer(&optimization_data, {});
        if (file_optimizer.Initialize(input_filename, output_filename, "spirv-simulator"))
        {
            file_optimizer.Process();

            if (file_optimizer.GetErrorState() != gfxrecon::decode::kErrorNone &&
                file_optimizer.GetErrorState() != gfxrecon::decode::FileTransformer::Error::kErrorReadingBlockHeader)
            {
                throw std::runtime_error("A failure has occurred during file processing");
            }

            GFXRECON_WRITE_CONSOLE("Spirv tracker modification complete.");
            GFXRECON_WRITE_CONSOLE("	Original file size: %" PRIu64 " bytes", file_optimizer.GetNumBytesRead());
            GFXRECON_WRITE_CONSOLE("	Modified file size: %" PRIu64 " bytes", file_optimizer.GetNumBytesWritten());
        }
    }
}

int main(int argc, const char** argv)
{
    int64_t start_time = gfxrecon::util::datetime::GetTimestamp();

    gfxrecon::util::Log::Init();

    gfxrecon::util::ArgumentParser arg_parser(argc, argv, kOptions, kArguments);

    if (CheckOptionPrintUsage(argv[0], arg_parser) || CheckOptionPrintVersion(argv[0], arg_parser))
    {
        gfxrecon::util::Log::Release();
        exit(0);
    }
    else if (arg_parser.IsInvalid() || (arg_parser.GetPositionalArgumentsCount() < 1) ||
             (arg_parser.GetPositionalArgumentsCount() > 2))
    {
        PrintUsage(argv[0]);
        gfxrecon::util::Log::Release();
        exit(-1);
    }

    try
    {
        const std::vector<std::string>& positional_arguments = arg_parser.GetPositionalArguments();
        std::string                     input_filename       = positional_arguments[0];
        std::string output_filename = (positional_arguments.size() > 1) ? positional_arguments[1] : std::string();

        bool detected_d3d12  = false;
        bool detected_vulkan = false;
        bool detected_openxr = false;
        gfxrecon::decode::DetectAPIs(input_filename, detected_d3d12, detected_vulkan, detected_openxr);

        if ((!detected_d3d12) && (!detected_vulkan))
        {
            // Detect with no block limit
            gfxrecon::decode::DetectAPIs(input_filename, detected_d3d12, detected_vulkan, detected_openxr, true);
        }

        if (detected_d3d12)
        {
            // reserved for dx12
        }
        else if (detected_vulkan)
        {
            gfxrecon::decode::VulkanSpirvTrackerOptions tracker_options = {};
            tracker_options.verbose                                     = arg_parser.IsOptionSet(kVerboseOption);
            tracker_options.error_on_buffers_incomplete = arg_parser.IsOptionSet(kErrorOnBuffersIncomplete);
            tracker_options.fixup_json_path             = ResolveFixupJsonPath(input_filename, arg_parser);

            RunSpirvTracker(input_filename, output_filename, tracker_options);
        }
        else
        {
            GFXRECON_LOG_ERROR("Could not detect graphics API. Aborting optimization.")
        }
    }
    catch (const std::runtime_error& error)
    {
        GFXRECON_WRITE_CONSOLE("File processing has encountered a fatal error and cannot continue: %s", error.what());
        gfxrecon::util::Log::Release();
        return -1;
    }
    catch (...)
    {
        GFXRECON_WRITE_CONSOLE("File processing failed due to an unhandled exception");
        gfxrecon::util::Log::Release();
        return -1;
    }

    int64_t end_time        = gfxrecon::util::datetime::GetTimestamp();
    int     time_in_seconds = static_cast<int>(gfxrecon::util::datetime::ConvertTimestampToSeconds(
        gfxrecon::util::datetime::DiffTimestamps(start_time, end_time)));
    GFXRECON_WRITE_CONSOLE("File processing time: %d seconds", time_in_seconds);

    gfxrecon::util::Log::Release();
    return 0;
}