#pragma once
#include <reshade.hpp>
#include <unordered_set>
#include <cstdint>

namespace glassmask {

using namespace reshade::api;

// —— 完整定义：不要只前向声明 —— 
struct GlassClassifierConfig {
    std::unordered_set<uint32_t> seg_hex_colors; // 例：{ 0x9B2EDB }
};

class GlassClassifier {
public:
    GlassClassifier(device* dev, const GlassClassifierConfig& cfg)
        : dev_(dev), cfg_(cfg) {}

    bool is_glass_draw(command_list* cmd,
                       pipeline current_pipeline,
                       uint32_t vertices_per_instance) const;

private:
    bool match_segmentation_color(command_list* cmd,
                                  uint32_t vertices_per_instance,
                                  uint32_t& out_hex_rrggbb) const;

    bool heuristic_from_pipeline(pipeline /*p*/) const { return false; }

private:
    device* dev_ = nullptr;
    const GlassClassifierConfig& cfg_;
};

} // namespace glassmask
