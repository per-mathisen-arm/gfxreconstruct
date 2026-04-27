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

#ifndef GFXRECON_DECODE_VULKAN_QUERY_UTIL_H
#define GFXRECON_DECODE_VULKAN_QUERY_UTIL_H

#include "util/defines.h"
#include "util/platform.h"

#include "vulkan/vulkan_core.h"

#include <cstddef>
#include <cstdint>

GFXRECON_BEGIN_NAMESPACE(gfxrecon)
GFXRECON_BEGIN_NAMESPACE(decode)

static inline uint64_t ReadCapturedQueryResult(
    const uint8_t* data, size_t data_size, VkDeviceSize stride, uint32_t query_index, size_t value_size)
{
    const uint64_t offset64 = static_cast<uint64_t>(stride) * query_index;

    if (offset64 > data_size)
    {
        return 0;
    }

    const size_t offset = static_cast<size_t>(offset64);

    if ((offset > data_size) || (value_size > (data_size - offset)))
    {
        return 0;
    }

    uint64_t value = 0;
    util::platform::MemoryCopy(&value, sizeof(value), data + offset, value_size);
    return value;
}

GFXRECON_END_NAMESPACE(decode)
GFXRECON_END_NAMESPACE(gfxrecon)

#endif // GFXRECON_DECODE_VULKAN_QUERY_UTIL_H
