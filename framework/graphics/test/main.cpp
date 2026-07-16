///////////////////////////////////////////////////////////////////////////////
// Copyright(c) 2019 Advanced Micro Devices, Inc.All rights reserved
//
// Permission is hereby granted, free of charge, to any person obtaining a
// copy of this software and associated documentation files (the "Software"),
// to deal in the Software without restriction, including without limitation
// the rights to use, copy, modify, merge, publish, distribute, sublicense,
// and/or sell copies of the Software, and to permit persons to whom the
// Software is furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
// FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
// DEALINGS IN THE SOFTWARE.
//
/// \author AMD Developer Tools Team
/// \description gfxrecon_graphics test main entry point
///////////////////////////////////////////////////////////////////////////////

#include <numeric>

#define CATCH_CONFIG_MAIN
#include <catch2/catch.hpp>

#include "graphics/vulkan_resources_util.h"
#include "graphics/vulkan_shader_group_handle.h"

TEST_CASE("vulkan_shader_group_handle - create empty handles", "[]")
{
    gfxrecon::graphics::shader_group_handle_t one, two;

    // check for all zeros
    uint8_t data[gfxrecon::graphics::shader_group_handle_t::MAX_HANDLE_SIZE] = {};
    REQUIRE(memcmp(one.data, data, gfxrecon::graphics::shader_group_handle_t::MAX_HANDLE_SIZE) == 0);

    REQUIRE(one == two);
    REQUIRE_FALSE(one != two);

    auto three = one;
    REQUIRE(one == three);
}

TEST_CASE("vulkan_shader_group_handle - create handles", "[]")
{
    std::vector<uint8_t> data(32);
    std::iota(data.begin(), data.end(), 0);
    gfxrecon::graphics::shader_group_handle_t one(data.data(), data.size());

    data[31] = 99;
    gfxrecon::graphics::shader_group_handle_t two(data.data(), data.size());
    REQUIRE(one != two);

    // check hashing via std::hash
    std::hash<gfxrecon::graphics::shader_group_handle_t> hasher;
    REQUIRE(hasher(one) != hasher(two));
}

TEST_CASE("tensor format features are selected for the requested tiling", "[tensor]")
{
    VkTensorFormatPropertiesARM properties = { VK_STRUCTURE_TYPE_TENSOR_FORMAT_PROPERTIES_ARM };
    properties.optimalTilingTensorFeatures = VK_FORMAT_FEATURE_2_TRANSFER_SRC_BIT;
    properties.linearTilingTensorFeatures  = VK_FORMAT_FEATURE_2_TRANSFER_DST_BIT;

    REQUIRE(gfxrecon::graphics::TensorFormatHasFeatures(
        properties, VK_TENSOR_TILING_OPTIMAL_ARM, VK_FORMAT_FEATURE_2_TRANSFER_SRC_BIT));
    REQUIRE_FALSE(gfxrecon::graphics::TensorFormatHasFeatures(
        properties, VK_TENSOR_TILING_OPTIMAL_ARM, VK_FORMAT_FEATURE_2_TRANSFER_DST_BIT));
    REQUIRE_FALSE(gfxrecon::graphics::TensorFormatHasFeatures(properties,
                                                              VK_TENSOR_TILING_OPTIMAL_ARM,
                                                              VK_FORMAT_FEATURE_2_TRANSFER_SRC_BIT |
                                                                  VK_FORMAT_FEATURE_2_TRANSFER_DST_BIT));
    REQUIRE(gfxrecon::graphics::TensorFormatHasFeatures(
        properties, VK_TENSOR_TILING_LINEAR_ARM, VK_FORMAT_FEATURE_2_TRANSFER_DST_BIT));
    REQUIRE_FALSE(gfxrecon::graphics::TensorFormatHasFeatures(
        properties, VK_TENSOR_TILING_LINEAR_ARM, VK_FORMAT_FEATURE_2_TRANSFER_SRC_BIT));
    REQUIRE_FALSE(gfxrecon::graphics::TensorFormatHasFeatures(
        properties, VK_TENSOR_TILING_MAX_ENUM_ARM, VK_FORMAT_FEATURE_2_TRANSFER_SRC_BIT));
    REQUIRE_FALSE(gfxrecon::graphics::TensorFormatHasFeatures(properties, VK_TENSOR_TILING_OPTIMAL_ARM, 0));

    properties.optimalTilingTensorFeatures |= VK_FORMAT_FEATURE_2_TRANSFER_DST_BIT;
    REQUIRE(gfxrecon::graphics::TensorFormatHasFeatures(properties,
                                                        VK_TENSOR_TILING_OPTIMAL_ARM,
                                                        VK_FORMAT_FEATURE_2_TRANSFER_SRC_BIT |
                                                            VK_FORMAT_FEATURE_2_TRANSFER_DST_BIT));
}

TEST_CASE("tensor staging memory selection falls back to plain host-visible memory", "[tensor]")
{
    VkPhysicalDeviceMemoryProperties properties{};
    properties.memoryTypeCount              = 4;
    properties.memoryTypes[0].propertyFlags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
    properties.memoryTypes[1].propertyFlags = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT;
    properties.memoryTypes[2].propertyFlags =
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
    properties.memoryTypes[3].propertyFlags = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_CACHED_BIT;

    uint32_t              index = VK_MAX_MEMORY_TYPES;
    VkMemoryPropertyFlags flags = 0;

    SECTION("cached memory is preferred")
    {
        REQUIRE(gfxrecon::graphics::FindTensorStagingMemoryTypeIndex(properties, 0xf, &index, &flags));
        REQUIRE(index == 3);
        REQUIRE(flags == properties.memoryTypes[3].propertyFlags);
    }

    SECTION("coherent memory is the second choice")
    {
        REQUIRE(
            gfxrecon::graphics::FindTensorStagingMemoryTypeIndex(properties, (1u << 1) | (1u << 2), &index, &flags));
        REQUIRE(index == 2);
        REQUIRE(flags == properties.memoryTypes[2].propertyFlags);
    }

    SECTION("plain host-visible memory is accepted")
    {
        REQUIRE(gfxrecon::graphics::FindTensorStagingMemoryTypeIndex(properties, (1u << 1), &index, &flags));
        REQUIRE(index == 1);
        REQUIRE(flags == properties.memoryTypes[1].propertyFlags);
    }

    SECTION("incompatible memory types are rejected")
    {
        REQUIRE_FALSE(gfxrecon::graphics::FindTensorStagingMemoryTypeIndex(properties, (1u << 0), &index, &flags));
    }
}
