/*
** Copyright (c) 2018 LunarG, Inc.
**
** Licensed under the Apache License, Version 2.0 (the "License");
** you may not use this file except in compliance with the License.
** You may obtain a copy of the License at
**
**     http://www.apache.org/licenses/LICENSE-2.0
**
** Unless required by applicable law or agreed to in writing, software
** distributed under the License is distributed on an "AS IS" BASIS,
** WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
** See the License for the specific language governing permissions and
** limitations under the License.
*/

#ifndef BRIMSTONE_DECODE_COMPRESSION_CONVERTER_H
#define BRIMSTONE_DECODE_COMPRESSION_CONVERTER_H

#include <string>

#include "decode/decoder.h"

#include "util/file_output_stream.h"
#include "util/memory_output_stream.h"
#include "util/compressor.h"

BRIMSTONE_BEGIN_NAMESPACE(brimstone)
BRIMSTONE_BEGIN_NAMESPACE(decode)

class CompressionConverter : public Decoder
{
  public:
    CompressionConverter();

    virtual ~CompressionConverter();

    bool Initialize(std::string                                filename,
                    const format::FileHeader&                  file_header,
                    const std::vector<format::FileOptionPair>& option_list,
                    util::CompressionType                      target_compression_type);

    void Destroy();

    virtual bool SupportsApiCall(format::ApiCallId call_id) override
    {
        // Blocks are not decoded, just compressed or decmpressed, so all are supported.
        return true;
    }

    virtual void DecodeFunctionCall(format::ApiCallId             call_id,
                                    const format::ApiCallOptions& call_options,
                                    const uint8_t*                buffer,
                                    size_t                        buffer_size);

    virtual void DispatchDisplayMessageCommand(const std::string& message) override;

    virtual void
                 DispatchFillMemoryCommand(uint64_t memory_id, uint64_t offset, uint64_t size, const uint8_t* data) override;

    virtual void DispatchResizeWindowCommand(format::HandleId surface_id, uint32_t width, uint32_t height) override;

    uint64_t     NumBytesWritten() { return bytes_written_; }

  private:
    std::unique_ptr<util::FileOutputStream> file_stream_;
    std::string                             filename_;
    uint64_t                                bytes_written_;
    std::vector<uint8_t>                    compressed_buffer_;
    util::Compressor*                       compressor_;
    bool                                    decompressing_;
    bool                                    write_thread_id_;
    bool                                    write_begin_end_times_;
};

BRIMSTONE_END_NAMESPACE(decode)
BRIMSTONE_END_NAMESPACE(brimstone)

#endif // BRIMSTONE_DECODE_COMPRESSION_CONVERTER_H