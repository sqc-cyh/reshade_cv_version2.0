#pragma once
#include <reshade.hpp>
#include <unordered_set>
#include <cstdint>

namespace glassmask {

using namespace reshade::api;

struct GlassClassifierConfig {
    // 来自 seg.png 的玻璃颜色集合（去掉 alpha，高 8 位无关）
    std::unordered_set<uint32_t> seg_hex_colors;
    uint32_t seg_color_seed = 0; // 与 segmentation 的 seed 一致时，完全可对齐
};

class GlassClassifier {
public:
    GlassClassifier(device* dev, const GlassClassifierConfig& cfg) : dev_(dev), cfg_(cfg) {}

    bool is_glass_draw(command_list* cmd,
                       pipeline current_pipeline,
                       uint32_t vertices_per_instance) const;

private:
    bool match_segmentation_color(command_list* cmd,
                                  uint32_t vertices_per_instance,
                                  uint32_t& out_hex_rrggbb) const;

    bool heuristic_from_pipeline(pipeline /*p*/) const { return false; } // 先占位

private:
    device* dev_ = nullptr;
    const GlassClassifierConfig& cfg_;
};

} // namespace glassmask
