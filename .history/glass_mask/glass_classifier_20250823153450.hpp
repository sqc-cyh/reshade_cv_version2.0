#pragma once
#include <reshade.hpp>
#include <unordered_set>
#include <cstdint>

namespace glassmask {

using namespace reshade::api;

// 供外部配置的简单结构：把“玻璃标签颜色”以 RGB24（0xRRGGBB）形式放入集合
struct GlassClassifierConfig {
    std::unordered_set<uint32_t> seg_hex_colors; // 例：{ 0x9B2EDB /* #9b2edb */ }
};

// 依赖 segmentation 的 per-draw 元数据颜色判别
class GlassClassifier {
public:
    GlassClassifier(device* dev, const GlassClassifierConfig& cfg)
        : dev_(dev), cfg_(cfg) {}

    // 主接口：判定“这一次 drawcall 是否为玻璃”
    // 注意：vertices_per_instance 传入 draw/draw_indexed 的顶点数或索引数
    bool is_glass_draw(command_list* cmd,
                       pipeline current_pipeline,
                       uint32_t vertices_per_instance) const;

private:
    // 从 segmentation 的 per-draw 元数据推导该 draw 的标签颜色（RGB24）
    bool match_segmentation_color(command_list* cmd,
                                  uint32_t vertices_per_instance,
                                  uint32_t& out_hex_rrggbb) const;

    // 先禁用基于 pipeline 的启发式（避免旧 API 依赖；需要时再用子对象重写）
    bool heuristic_from_pipeline(pipeline /*p*/) const { return false; }

private:
    device* dev_ = nullptr;
    const GlassClassifierConfig& cfg_;
};

} // namespace glassmask
