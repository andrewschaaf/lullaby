/*
Copyright 2017 Google Inc. All Rights Reserved.

Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at

    http://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS-IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License.
*/

#ifndef LULLABY_UTIL_MESH_H_
#define LULLABY_UTIL_MESH_H_

#include <stdint.h>
#include <algorithm>
#include <functional>
#include <vector>

#include "mathfu/glsl_mappings.h"
#include "lullaby/util/logging.h"

namespace lull {

// Bitmask for representing a set of quad corners.  These are named as if
// looking down the -z axis: +x = right and +y is top.
enum class CornerMask {
  kNone = 0,
  kTopRight = (1 << 0),
  kBottomRight = (1 << 1),
  kBottomLeft = (1 << 2),
  kTopLeft = (1 << 3),
  kAll = kTopRight | kBottomRight | kBottomLeft | kTopLeft,
};
inline CornerMask operator|(CornerMask a, CornerMask b) {
  return static_cast<CornerMask>(static_cast<uint32_t>(a) |
                                 static_cast<uint32_t>(b));
}

inline CornerMask operator&(CornerMask a, CornerMask b) {
  return static_cast<CornerMask>(static_cast<uint32_t>(a) &
                                 static_cast<uint32_t>(b));
}

// Generates a list of indices that can be used with the vertices returned by
// CalculateTesselatedQuadVertices when rendering them as triangles.
std::vector<uint16_t> CalculateTesselatedQuadIndices(int num_verts_x,
                                                     int num_verts_y,
                                                     int corner_verts);

// BUG(b/28863495) Remove this when mesh consolidation is complete.
void ApplyDeformation(float* vertices, size_t len, size_t stride,
                      std::function<mathfu::vec3(const mathfu::vec3&)> deform);

// Generates a list of vertices containing that represent a tessellated
// rectangle. The vertices represent a |verts_x| by |verts_y| grid with
// positions from -size/2 to size/2 in each axis. If |corner_verts| > 0 the code
// will generate triangle fans around the corners of the tesselated quad of size
// |corner_radius|. It is assumed that |corner_radius| is small compared to
// |size_x| and |size_y|, otherwise the additional corner geometry will not
// deform correctly.  |corner_mask| only applies if |corner_verts| > 0, and
// does not affect the number of vertices generated, only their positions.
template <typename Vertex>
std::vector<Vertex> CalculateTesselatedQuadVertices(
    float size_x, float size_y, int num_verts_x, int num_verts_y,
    float corner_radius, int corner_verts,
    CornerMask corner_mask = CornerMask::kAll) {
  if (size_x < 0.f || size_y < 0.f) {
    LOG(DFATAL) << "Size of quad has to be >= than 0.0";
    return std::vector<Vertex>();
  }
  corner_radius = std::min(corner_radius, std::min(size_x, size_y) / 2.0f);
  if (corner_verts > 0) {
    // We reserve 2 additional verts in each dimension to generate the "tabs"
    // that overhang the central quad on the sides for the triangle fan to
    // connect to.
    if (num_verts_x < 4 || num_verts_y < 4) {
      LOG(DFATAL) << "Failed to reserve 4 additional vertices.";
      return std::vector<Vertex>();
    }
  } else if (corner_verts == 0) {
    if (num_verts_x < 2 || num_verts_y < 2) {
      LOG(DFATAL) << "Failed to reserve 2 additional vertices.";
      return std::vector<Vertex>();
    }
  } else {
    LOG(DFATAL) << "Must have >= 0 corner vertices.";
    return std::vector<Vertex>();
  }

  // Define each vertex in column major order:
  //
  //  2---5---8
  //  |   |   |
  //  1---4---7
  //  |   |   |
  //  0---3---6
  //
  //  If corner_verts is nonzero we add the tabs on each side of the interior in
  //  order as detailed here:
  //
  //      D       F
  //      +-------+        ^                  ^
  //      |       |        | corner_radius    |
  // B +--+-------+--+ H   v                  |
  //   |  |       |  |                        |
  //   |  |       |  |                        | size_y
  //   |  |       |  |                        |
  // A +--+-------+--+ G  ^                   |
  //      |       |       | corner_radius     |
  //      +-------+       v                   v
  //      C       E
  //
  //  <-->        <-->
  //    corner_radius
  //
  //  <-------------->
  //       size_x
  //

  const float half_size_x = size_x / 2.0f;
  const float half_size_y = size_y / 2.0f;
  const float interior_size_x = size_x - (2.0f * corner_radius);
  const float interior_size_y = size_y - (2.0f * corner_radius);
  const float half_interior_size_x = interior_size_x / 2.0f;
  const float half_interior_size_y = interior_size_y / 2.0f;
  const float u_texture_inset = corner_radius / size_x;
  const float u_texture_range = 1.0f - (2.0f * u_texture_inset);
  const float v_texture_inset = corner_radius / size_y;
  const float v_texture_range = 1.0f - (2.0f * v_texture_inset);
  const float z = 0.0f;
  const int num_interior_verts_x =
      corner_verts > 0 ? num_verts_x - 2 : num_verts_x;
  const int num_interior_verts_y =
      corner_verts > 0 ? num_verts_y - 2 : num_verts_y;
  const int num_corner_verts =
      (corner_verts > 0
           ? (4 * corner_verts) + (2 * num_interior_verts_x) +
                 (2 * num_interior_verts_y)
           : 0);
  // The radiused corners add |corner_verts| vertices for each of the four
  // corners as well as an additional line of interior verts on each side
  // of the quad for the tabs.
  const size_t num_verts =
      ((num_interior_verts_x * num_interior_verts_y) + num_corner_verts);
  std::vector<Vertex> vertices(num_verts);
  size_t index = 0;

  if (corner_verts > 0) {
    // Build the left tab as described by A and B into the interior square in
    // the above tabs diagram.
    for (int y = 0; y < num_interior_verts_y; ++y) {
      const float y_fraction =
          static_cast<float>(y) / static_cast<float>(num_interior_verts_y - 1);
      const float y_val = (y_fraction * interior_size_y) - half_interior_size_y;
      SetPosition(&vertices[index], -half_size_x, y_val, z);
      SetUv0(&vertices[index], 0.f,
             v_texture_inset + ((1.0f - y_fraction) * v_texture_range));
      ++index;
    }
  }

  // Build interior rectangle verts + vertical tabs if needed, the square
  // described by |CDFE| in the tabs diagram.
  for (int x = 0; x < num_interior_verts_x; ++x) {
    // Calculate what fraction of the entire rect this vertex is at.
    const float x_fraction =
        static_cast<float>(x) / static_cast<float>(num_interior_verts_x - 1);
    // Then use that fraction to calculate the actual position.
    const float x_val = (x_fraction * interior_size_x) - half_interior_size_x;
    // And lastly the texture u coordinate.
    const float u_val = u_texture_inset + (x_fraction * u_texture_range);

    // Append a lower tab vertex if needed.
    if (corner_verts > 0) {
      SetPosition(&vertices[index], x_val, -half_size_y, z);
      SetUv0(&vertices[index], u_val, 1.0f);
      ++index;
    }

    for (int y = 0; y < num_interior_verts_y; ++y) {
      const float y_fraction =
          static_cast<float>(y) / static_cast<float>(num_interior_verts_y - 1);
      const float y_val = (y_fraction * interior_size_y) - half_interior_size_y;

      SetPosition(&vertices[index], x_val, y_val, z);
      // Flip the v coordinate, as fpl has 0,0 at top left
      SetUv0(&vertices[index], u_val,
             v_texture_inset + ((1.0f - y_fraction) * v_texture_range));
      ++index;
    }

    // Append a upper tab vertex if needed.
    if (corner_verts > 0) {
      SetPosition(&vertices[index], x_val, half_size_y, z);
      SetUv0(&vertices[index], u_val, 0.f);
      ++index;
    }
  }

  if (corner_verts > 0) {
    // Build the right tab as described by H and G.
    for (int y = 0; y < num_interior_verts_y; ++y) {
      const float y_fraction =
          static_cast<float>(y) / static_cast<float>(num_interior_verts_y - 1);
      const float y_val = (y_fraction * interior_size_y) - half_interior_size_y;
      SetPosition(&vertices[index], half_size_x, y_val, 0.f);
      SetUv0(&vertices[index], 1.f,
             v_texture_inset + ((1.0f - y_fraction) * v_texture_range));
      ++index;
    }

    // Compute fan vertices.
    mathfu::vec2 lower_left_xy(-half_interior_size_x, -half_interior_size_y);
    mathfu::vec2 upper_left_xy(-half_interior_size_x, half_interior_size_y);
    mathfu::vec2 lower_right_xy(half_interior_size_x, -half_interior_size_y);
    mathfu::vec2 upper_right_xy(half_interior_size_x, half_interior_size_y);
    const float u_texture_far_inset = 1.0f - u_texture_inset;
    const float v_texture_far_inset = 1.0f - v_texture_inset;
    mathfu::vec2 lower_left_uv(u_texture_inset, v_texture_far_inset);
    mathfu::vec2 upper_left_uv(u_texture_inset, v_texture_inset);
    mathfu::vec2 lower_right_uv(u_texture_far_inset, v_texture_far_inset);
    mathfu::vec2 upper_right_uv(u_texture_far_inset, v_texture_inset);
    mathfu::vec2 uv_scale(1.0f / size_x, -1.0f / size_y);

    auto unround_corner = [corner_radius](const mathfu::vec2& offset) {
      const float absx = fabsf(offset.x);
      const float absy = fabsf(offset.y);
      const float scale = corner_radius / std::max(absx, absy);
      return offset * scale;
    };

    for (int i = 0; i < corner_verts; ++i) {
      const float theta =
          (static_cast<float>(i + 1) / static_cast<float>(corner_verts)) *
          (static_cast<float>(M_PI) / 2.0f);
      const float r_sin_theta = corner_radius * sinf(theta);
      const float r_cos_theta = corner_radius * cosf(theta);
      // Compute lower left radius vertex.
      mathfu::vec2 ll_offset = mathfu::vec2(-r_sin_theta, -r_cos_theta);
      if ((corner_mask & CornerMask::kBottomLeft) == CornerMask::kNone) {
        ll_offset = unround_corner(ll_offset);
      }
      mathfu::vec2 ll_xy = lower_left_xy + ll_offset;
      mathfu::vec2 ll_uv = lower_left_uv + (ll_offset * uv_scale);
      SetPosition(&vertices[index], ll_xy.x, ll_xy.y, z);
      SetUv0(&vertices[index], ll_uv);
      ++index;
      // Compute upper left radius vertex.
      mathfu::vec2 ul_offset = mathfu::vec2(-r_cos_theta, r_sin_theta);
      if ((corner_mask & CornerMask::kTopLeft) == CornerMask::kNone) {
        ul_offset = unround_corner(ul_offset);
      }
      mathfu::vec2 ul_xy = upper_left_xy + ul_offset;
      mathfu::vec2 ul_uv = upper_left_uv + (ul_offset * uv_scale);
      SetPosition(&vertices[index], ul_xy.x, ul_xy.y, z);
      SetUv0(&vertices[index], ul_uv);
      ++index;
      // Compute lower right radius vertex.
      mathfu::vec2 lr_offset = mathfu::vec2(r_cos_theta, -r_sin_theta);
      if ((corner_mask & CornerMask::kBottomRight) == CornerMask::kNone) {
        lr_offset = unround_corner(lr_offset);
      }
      mathfu::vec2 lr_xy = lower_right_xy + lr_offset;
      mathfu::vec2 lr_uv = lower_right_uv + (lr_offset * uv_scale);
      SetPosition(&vertices[index], lr_xy.x, lr_xy.y, z);
      SetUv0(&vertices[index], lr_uv);
      ++index;
      // Compute upper right radius vertex.
      mathfu::vec2 ur_offset = mathfu::vec2(r_sin_theta, r_cos_theta);
      if ((corner_mask & CornerMask::kTopRight) == CornerMask::kNone) {
        ur_offset = unround_corner(ur_offset);
      }
      mathfu::vec2 ur_xy = upper_right_xy + ur_offset;
      mathfu::vec2 ur_uv = upper_right_uv + (ur_offset * uv_scale);
      SetPosition(&vertices[index], ur_xy.x, ur_xy.y, z);
      SetUv0(&vertices[index], ur_uv);
      ++index;
    }
  }

  // Should have filled exactly the array.
  DCHECK_EQ(num_verts, index) << "Failed to fill vertices array!";
  return vertices;
}

}  // namespace lull

#endif  // LULLABY_UTIL_MESH_H_
