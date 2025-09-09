#include "glass_classifier.hpp"
#include "seg_bridge.hpp"  // 使用桥接
#include <cstring>

using namespace reshade::api;

namespace glassmask {

bool GlassClassifier::match_segmentation_color(command_list* cmd,
                                               uint32_t vertices_per_instance,
                                               uint32_t& out_hex) const
{
    if (cfg_.seg_hex_colors.empty()) return false;

    // 与 segmentation 完全一致的元数据与 hash 算法
    auto meta = seg::get_draw_metadata_including_shader_info(dev_, cmd, vertices_per_instance);
    const uint32_t hex_full = seg::colorhashfun(meta.data(), sizeof(meta), cfg_.seg_color_seed);
    const uint32_t hex24    = (hex_full & 0x00FFFFFFu); // 丢弃固定的 alpha=255
    out_hex = hex24;

    return (cfg_.seg_hex_colors.count(hex24) != 0);
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
