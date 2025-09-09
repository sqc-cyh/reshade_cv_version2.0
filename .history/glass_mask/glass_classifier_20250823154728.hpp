#pragma once
#include <reshade.hpp>
#include <unordered_set>
#include <cstdint>

struct GlassClassifierConfig;

class GlassClassifier {
public:
  GlassClassifier(reshade::api::device* dev, const GlassClassifierConfig& cfg);

  // 统一成三个参数：cmd（取 per-draw 元数据要用），当前 pipeline，以及“本次 draw 的顶点/索引数”
  bool is_glass_draw(reshade::api::command_list* cmd,
                     reshade::api::pipeline pipeline,
                     uint32_t vertices_per_instance) const;

private:
  // 需要 cmd 和 vertices_per_instance 才能从 segmentation 拿到 per-draw 元数据
  bool match_segmentation_color(reshade::api::command_list* cmd,
                                uint32_t vertices_per_instance,
                                uint32_t& out_hex_rrggbb) const;

  bool heuristic_from_pipeline(reshade::api::pipeline p) const;

private:
  reshade::api::device* dev_;
  const GlassClassifierConfig& cfg_;
};
