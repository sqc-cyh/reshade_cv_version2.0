#include "seg_public_api.hpp"

// 仅在实现文件里包含 segmentation 的内部头
#include "segmentation_app_data.hpp"
#include "command_list_state.hpp"
#include "buffer_indexing_colorization.hpp" // colorhashfun 的定义位置
#include "semseg_shader_register_bind.hpp"

namespace segpub {

perdraw_metadata_type get_draw_metadata_including_shader_info(
    reshade::api::device* device,
    reshade::api::command_list* cmd_list,
    uint32_t draw_num_vertices)
{
  using namespace reshade::api;
  auto& app  = device->get_private_data<segmentation_app_data>();
  auto& st   = cmd_list->get_private_data<segmentation_app_cmdlist_state>();

  perdraw_metadata_type ret = { draw_num_vertices, 0ull, 0ull };

  for (const auto& kv : st.pipelines) {
    const pipeline_stage stages = kv.first;
    const pipeline       pipe   = kv.second;
    if (pipe.handle == 0ull) continue;

    const bool has_vs = (static_cast<uint32_t>(stages) &
                         static_cast<uint32_t>(pipeline_stage::vertex_shader)) != 0u;
    const bool has_ps = (static_cast<uint32_t>(stages) &
                         static_cast<uint32_t>(pipeline_stage::pixel_shader))  != 0u;

    if (has_vs) {
      auto it = app.pipeline_handle_to_vertex_shader_hash.find(pipe.handle);
      if (it != app.pipeline_handle_to_vertex_shader_hash.end())
        ret[1] = it->second;
    }
    if (has_ps) {
      auto it = app.pipeline_handle_to_pixel_shader_hash.find(pipe.handle);
      if (it != app.pipeline_handle_to_pixel_shader_hash.end())
        ret[2] = it->second;
    }
  }
  return ret;
}

// 直接复用 segmentation 的实现
uint32_t colorhashfun(const void* buf, size_t buflen, uint32_t seed) {
  return ::colorhashfun(buf, buflen, seed);
}

} // namespace segpub
