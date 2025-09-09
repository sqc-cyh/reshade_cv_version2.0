#include "glass_classifier.hpp"
#include <cstring>
#include <cstdint>

#include "segmentation/semseg_shader_register_bind.hpp"
#include "segmentation/buffer_indexing_colorization.hpp"

// ↑ 上述两个头在你的 segmentation 工程中已存在；
//   它们提供 per-draw 元数据与 colorhashfun 等工具。

namespace glassmask {

using namespace reshade::api;

// 计算当前 draw 的 segmentation 颜色（RGB24，0xRRGGBB）
bool GlassClassifier::match_segmentation_color(command_list* cmd,
                                               uint32_t vertices_per_instance,
                                               uint32_t& out_hex) const
{
    if (cfg_.seg_hex_colors.empty())
        return false;

    // 从 segmentation 拿 per-draw 元数据（接口来自你的现有代码库）
    // 注意：下面类型/函数名与现有 segmentation 的实现保持一致
    perdraw_metadata_type meta =
        get_draw_metadata_including_shader_info(dev_, cmd, vertices_per_instance);

    // 用 segmentation 的 hash 方式得到该 draw 的颜色（与 seg.png 一致）
    constexpr uint32_t kSeed = 0; // 如你的实现有其它 seed，请保持一致
    const uint32_t hex = colorhashfun(&meta, sizeof(meta), kSeed) & 0xFFFFFF;

    out_hex = hex;
    return (cfg_.seg_hex_colors.find(hex) != cfg_.seg_hex_colors.end());
}

bool GlassClassifier::is_glass_draw(command_list* cmd,
                                    pipeline current_pipeline,
                                    uint32_t vertices_per_instance) const
{
    uint32_t seg_hex = 0;
    if (match_segmentation_color(cmd, vertices_per_instance, seg_hex))
        return true;

    // 备用启发式（暂关）
    if (heuristic_from_pipeline(current_pipeline))
        return true;

    return false;
}

} // namespace glassmask
