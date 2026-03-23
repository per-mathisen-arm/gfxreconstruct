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

#include "../optimize/vulkan_spirv_tracker_modifier.h"
#include "../tool_settings.h"

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

const char kVerboseOption[] = "--verbose";

const char kOptions[]   = "-h|--help,--version,--verbose";
const char kArguments[] = "--gpu,--set-replay-options,--set-replay-options";

static void PrintUsage(const char* exe_name)
{
    std::string app_name     = exe_name;
    size_t      dir_location = app_name.find_last_of("/\\");
    if (dir_location >= 0)
    {
        app_name.replace(0, dir_location + 1, "");
    }
    GFXRECON_WRITE_CONSOLE("\n%s - Run the spirv-simulator and print the output from simulator.", app_name.c_str());

    GFXRECON_WRITE_CONSOLE("");
    GFXRECON_WRITE_CONSOLE("Usage:");
    GFXRECON_WRITE_CONSOLE("  %s [-h | --help] [--version] [--verbose] <input-file>", app_name.c_str());
    GFXRECON_WRITE_CONSOLE("");
    GFXRECON_WRITE_CONSOLE("Required arguments:");
    GFXRECON_WRITE_CONSOLE("  <input-file>\t\tThe path to input GFXReconstruct capture file to be processed.");
    GFXRECON_WRITE_CONSOLE("");
}

void GetSpirvSimulatorData(const std::string& input_filename, bool verbose)
{
    gfxrecon::decode::FileProcessor file_processor;
    if (file_processor.Initialize(input_filename))
    {
        gfxrecon::decode::VulkanDecoder decoder;
        auto spirv_tracker_modifier_consumer = std::make_unique<gfxrecon::decode::VulkanSpirvTrackModifier>(verbose);

        decoder.AddConsumer(spirv_tracker_modifier_consumer.get());

        file_processor.AddDecoder(&decoder);
        file_processor.ProcessAllFrames();

        if (file_processor.GetErrorState() != gfxrecon::decode::kErrorNone)
        {
            throw std::runtime_error("Failed to scan input file for optimizations");
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
    else if (arg_parser.IsInvalid() || (arg_parser.GetPositionalArgumentsCount() != 1))
    {
        PrintUsage(argv[0]);
        gfxrecon::util::Log::Release();
        exit(-1);
    }

    try
    {
        const std::vector<std::string>& positional_arguments = arg_parser.GetPositionalArguments();
        std::string                     input_filename       = positional_arguments[0];

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
            GetSpirvSimulatorData(input_filename, arg_parser.IsOptionSet(kVerboseOption));
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