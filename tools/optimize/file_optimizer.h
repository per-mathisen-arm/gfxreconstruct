/*
** Copyright (c) 2020 LunarG, Inc.
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

#ifndef GFXRECON_FILE_OPTIMIZER_H
#define GFXRECON_FILE_OPTIMIZER_H

#include "decode/file_transformer.h"
#include "util/defines.h"

#include <unordered_set>

GFXRECON_BEGIN_NAMESPACE(gfxrecon)

class FileOptimizer : public decode::FileTransformer
{
  public:
    FileOptimizer(){};

    FileOptimizer(const std::unordered_set<format::HandleId>& unreferenced_ids);

    FileOptimizer(std::unordered_set<format::HandleId>&& unreferenced_ids);

    void SetUnreferencedBlocks(const std::unordered_set<uint64_t>& unreferenced_blocks);
    void SetRemovedThreads(const std::unordered_set<format::ThreadId>& removed_threads_ids);

    uint64_t GetUnreferencedBlocksSize();

  protected:
    bool ProcessFunctionCall(decode::ParsedBlock& parsed_block) override;
    bool ProcessMethodCall(decode::ParsedBlock& parsed_block) override;
    bool ProcessMetaData(decode::ParsedBlock& parsed_block) override;

    VisitResult FilterMetaData(const decode::InitBufferArgs& args);
    VisitResult FilterMetaData(const decode::InitImageArgs& args);
    VisitResult FilterMetaData(const decode::InitTensorArgs& args);
    template <typename Args>
    VisitResult FilterMetaData(const Args& args)
    {
        if constexpr (decode::DispatchFlagTraits<Args>::kHasThreadId)
        {
            if (removed_threads_ids_.contains(args.thread_id))
            {
                return kSuccess;
            }
        }
        else if constexpr (decode::DispatchFlagTraits<Args>::kHasCommandHeader)
        {
            if constexpr (decode::DispatchFlagTraits<decltype(args.command_header)>::kHasThreadId)
            {
                if (removed_threads_ids_.contains(args.command_header.thread_id))
                {
                    return kSuccess;
                }
            }
        }

        return kNeedsPassthrough;
    }

    bool FilterFunctionCall(const decode::FunctionCallArgs& args);
    bool FilterMethodCall(const decode::MethodCallArgs& args);

  private:
    std::unordered_set<format::HandleId> unreferenced_ids_;
    std::unordered_set<uint64_t>         unreferenced_blocks_;

    std::unordered_set<format::ThreadId> removed_threads_ids_;
};

GFXRECON_END_NAMESPACE(gfxrecon)

#endif // GFXRECON_FILE_OPTIMIZER_H
