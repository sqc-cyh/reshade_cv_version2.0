#include "glass_classifier.hpp"
#include "segmentation/seg_public_api.hpp"  // 使用桥接
#include <cstring>

using namespace reshade::api;

bool GlassClassifier::match_segmentation_color(command_list* cmd,
                                               uint32_t vertices_per_instance,
                                               uint32_t& out_hex) const
{
  if (cfg_.seg_hex_colors.empty()) return false;

  const auto meta = segpub::get_draw_metadata_including_shader_info(dev_, cmd, vertices_per_instance);
  const uint32_t hex = segpub::colorhashfun(&meta, sizeof(meta), cfg_.seg_color_seed) & 0xFFFFFFu;

  out_hex = hex;
  return (cfg_.seg_hex_colors.count(hex) != 0);
}


bool GlassClassifier::is_glass_draw(command_list* cmd,
                                    pipeline current_pipeline,
                                    uint32_t vertices_per_instance) const
{
    uint32_t seg_hex = 0;
    if (match_segmentation_color(cmd, vertices_per_instance, seg_hex))
        return true;

    // 需要时再补充：根据 pipeline 的启发式兜底
    if (heuristic_from_pipeline(current_pipeline))
        return true;

    return false;
}

} // namespace glassmask
