#pragma once
#include <unordered_set>
#include <cstdint>
#include <reshade.hpp>

namespace glassmask {

struct GlassClassifierConfig {
    // 来自 seg.png 的玻璃颜色集合（去掉 alpha，高 8 位无关）
    std::unordered_set<uint32_t> seg_hex_colors;
    uint32_t seg_color_seed = 0; // 与 segmentation 的 seed 一致时，完全可对齐
};

class GlassClassifier {
public:
    GlassClassifier(reshade::api::device* d, const GlassClassifierConfig& c)
        : dev_(d), cfg_(c) {}

    bool match_segmentation_color(reshade::api::command_list* cmd,
                                  uint32_t vertices_per_instance,
                                  uint32_t& out_hex) const;

    bool is_glass_draw(reshade::api::command_list* cmd,
                       reshade::api::pipeline current_pipeline,
                       uint32_t vertices_per_instance) const;

private:
    bool heuristic_from_pipeline(reshade::api::pipeline) const; // 你的启发式（可留空或简单实现）

    reshade::api::device* dev_ = nullptr;
    GlassClassifierConfig  cfg_{};
};

} // namespace glassmask
