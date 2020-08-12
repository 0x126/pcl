/*
 * SPDX-License-Identifier: BSD-3-Clause
 *
 *  Point Cloud Library (PCL) - www.pointclouds.org
 *  Copyright (c) 2020-, Open Perception
 *
 *  All rights reserved
 */

#pragma once

#include "morton.hpp"
#include <assert.h>

#include <limits>
#include <tuple>

namespace pcl {
namespace device {

__device__ __host__ __forceinline__ int
getBitsNum(int integer)
{
  int count = 0;
  while (integer > 0) {
    if (integer & 1)
      ++count;
    integer >>= 1;
  }
  return count;
}

__host__ __device__ __forceinline__ std::pair<uint3, std::uint8_t>
nearestVoxel(const float3 query,
             const unsigned& level,
             const std::uint8_t& mask,
             const float3& minp,
             const float3& maxp,
             const uint3& index)
{
  assert(mask != 0);
  // identify closest voxel
  float closest_distance = std::numeric_limits<float>::max();
  unsigned closest_index = 0;
  uint3 closest = make_uint3(0, 0, 0);

  for (unsigned i = 0; i < 8; ++i) {
    if ((mask & (1 << i)) == 0) // no child
      continue;

    const uint3 child = make_uint3((index.x << 1) + (i & 1),
                                   (index.y << 1) + ((i >> 1) & 1),
                                   (index.z << 1) + ((i >> 2) & 1));

    // find center of child cell
    const unsigned voxels_per_side = 1 << (level + 2);
    const float3 voxel_center =
        make_float3(minp.x + (maxp.x - minp.x) * (2 * child.x + 1) / voxels_per_side,
                    minp.y + (maxp.y - minp.y) * (2 * child.y + 1) / voxels_per_side,
                    minp.z + (maxp.z - minp.z) * (2 * child.z + 1) / voxels_per_side);

    // compute distance to centroid
    const float3 dist = make_float3(
        voxel_center.x - query.x, voxel_center.y - query.y, voxel_center.z - query.z);

    const float distance_to_query = dist.x * dist.x + dist.y * dist.y + dist.z * dist.z;

    // compare distance
    if (distance_to_query < closest_distance) {
      closest_distance = distance_to_query;
      closest_index = i;
      closest = child;
    }
  }

  return std::pair<uint3, std::uint8_t>(closest, 1 << closest_index);
}

#pragma hd_warning_disable
template <typename T>
__device__ __host__ int
findNode(const float3 minp, const float3 maxp, const float3 query, const T nodes)
{
  size_t node_idx = 0;
  const auto code = CalcMorton(minp, maxp)(query);
  unsigned level = 0;

  bool voxel_traversal = false;
  uint3 index = Morton::decomposeCode(code);
  std::uint8_t mask_pos;

  while (true) {
    const auto node = nodes[node_idx];
    const std::uint8_t mask = node & 0xFF;

    if (!mask) // leaf
      return node_idx;

    if (voxel_traversal) // empty voxel already encountered, performing nearest-centroid
                         // based traversal
    {
      const auto nearest_voxel = nearestVoxel(query, level, mask, minp, maxp, index);
      index = nearest_voxel.first;
      mask_pos = nearest_voxel.second;
    }
    else {
      mask_pos = 1 << Morton::extractLevelCode(code, level);

      if (!(mask & mask_pos)) // child doesn't exist
      {
        const auto remaining_depth = Morton::levels - level;
        index.x >>= remaining_depth;
        index.y >>= remaining_depth;
        index.z >>= remaining_depth;

        voxel_traversal = true;
        const auto nearest_voxel = nearestVoxel(query, level, mask, minp, maxp, index);
        index = nearest_voxel.first;
        mask_pos = nearest_voxel.second;
      }
    }
    #ifndef __CUDA_ARCH__ // use cpu function for bit count
      node_idx = (node >> 8) + getBitsNum(mask & (mask_pos - 1));
    #else // use cuda function for bit count
      node_idx = (node >> 8) + __popc(mask & (mask_pos - 1));
#endif

    ++level;
  }
}
} // namespace device
} // namespace pcl
