#pragma once
#include <array>
#include <cstdint>
#include <reshade.hpp>

// 引入 segmentation 中的状态定义（已在你仓库里）
#include "segmentation_app_data.hpp"
#include "command_list_state.hpp"

// xxhash 与 segmentation 一致
#include "xxhash.h"

namespace glassmask {
namespace seg {

using perdraw_metadata_type = std::array<uint64_t, 3>; // (#vertices, vtxShaderHash, pixShaderHash)

// 与 segmentation 完全一致的颜色哈希：高 8 位固定 255
inline uint32_t colorhashfun(const void* buf, size_t len, uint32_t seed) {
    uint32_t tmp = XXH32(buf, len, seed);
    reinterpret_cast<uint8_t*>(&tmp)[3] = 255;
    return tmp;
}

// 读取 segmentation 在 command_list / device 的 private_data，拼 per-draw 元数据
inline perdraw_metadata_type get_draw_metadata_including_shader_info(
    reshade::api::device* device,
    reshade::api::command_list* cmd_list,
    uint32_t draw_num_vertices)
{
    auto& mapp = device->get_private_data<segmentation_app_data>();
    auto& state = cmd_list->get_private_data<segmentation_app_cmdlist_state>();

    perdraw_metadata_type ret { draw_num_vertices, 0ull, 0ull };
    for (const auto& kv : state.pipelines) {
        const auto stages = static_cast<uint32_t>(kv.first);
        const auto handle = kv.second.handle;
        if (handle == 0ull) continue;

        if ((stages & static_cast<uint32_t>(reshade::api::pipeline_stage::vertex_shader)) &&
            mapp.pipeline_handle_to_vertex_shader_hash.count(handle)) {
            ret[1] = mapp.pipeline_handle_to_vertex_shader_hash.at(handle);
        }
        if ((stages & static_cast<uint32_t>(reshade::api::pipeline_stage::pixel_shader)) &&
            mapp.pipeline_handle_to_pixel_shader_hash.count(handle)) {
            ret[2] = mapp.pipeline_handle_to_pixel_shader_hash.at(handle);
        }
    }
    return ret;
}

} // namespace seg
} // namespace glassmask
