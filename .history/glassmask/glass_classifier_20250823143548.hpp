#pragma once
#include <reshade.hpp>
#include <unordered_set>
#include <cstdint>

struct GlassClassifierConfig;

class GlassClassifier {
public:
  GlassClassifier(reshade::api::device* dev, const GlassClassifierConfig& cfg);

  // 根据 segmentation 颜色或管线启发式判断当前 draw 是否“玻璃”
  bool is_glass_draw(reshade::api::command_list* cmd,
                     reshade::api::pipeline pipeline) const;

private:
  bool match_segmentation_color(uint32_t& out_hex_rrggbb) const;
  bool heuristic_from_pipeline(reshade::api::pipeline p) const;

private:
  reshade::api::device* dev_;
  const GlassClassifierConfig& cfg_;
};
