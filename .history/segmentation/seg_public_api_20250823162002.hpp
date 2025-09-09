#pragma once
#include <array>
#include <cstdint>
#include <reshade.hpp>

namespace segpub {  // 独立命名空间，避免和原工程内部命名冲突
  using perdraw_metadata_type = std::array<uint64_t, 3>;  // (#verts, VS hash, PS hash)

  // 由 segmentation 工程提供实现
  perdraw_metadata_type get_draw_metadata_including_shader_info(
      reshade::api::device* dev,
      reshade::api::command_list* cmd_list,
      uint32_t draw_num_vertices);

  // 与 segmentation UI 使用的哈希完全一致
  uint32_t colorhashfun(const void* buf, size_t buflen, uint32_t seed);
}  // namespace segpub
