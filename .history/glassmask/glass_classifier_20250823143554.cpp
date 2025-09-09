#include "glass_classifier.hpp"
#include "glassmask.hpp"
using namespace reshade::api;

GlassClassifier::GlassClassifier(device* dev, const GlassClassifierConfig& cfg)
  : dev_(dev), cfg_(cfg) {}

/// ====== 需要你接线的地方（见下文“缺少的功能”） ======
static bool query_current_draw_seg_hex(uint32_t& hex_rrggbb) {
  // TODO: 用你的 segmentation 运行时接口替换下面的占位返回。
  // 要求：如果能得到“当前 draw 对应的分割颜色（0xRRGGBB）”，返回 true 并写入 hex_rrggbb；
  // 否则返回 false。
  (void)hex_rrggbb;
  return false;
}

bool GlassClassifier::match_segmentation_color(uint32_t& out_hex) const {
  if (cfg_.seg_hex_colors.empty()) return false;
  uint32_t hex = 0;
  if (!query_current_draw_seg_hex(hex)) return false;
  out_hex = hex;
  return (cfg_.seg_hex_colors.count(hex) > 0);
}

/// 启发式（回退）：alpha blend 开且深度写关 → 认为是透明（可能是玻璃）
bool GlassClassifier::heuristic_from_pipeline(pipeline p) const {
  if (!cfg_.use_heuristic) return false;
  if (p.handle == 0) return false;

  device* dev = dev_;
  pipeline_desc desc = {};
  if (!dev->get_pipeline_desc(p, &desc)) return false;

  // blend 是否启用
  bool blend_on = false;
  for (uint32_t i = 0; i < desc.graphics.blend_state.num_attachments; ++i) {
    if (desc.graphics.blend_state.attachments[i].blend_enable) { blend_on = true; break; }
  }
  // 深度写入是否关闭
  bool depth_write_off = !desc.graphics.depth_stencil_state.depth_write_mask;

  return blend_on && depth_write_off;
}

bool GlassClassifier::is_glass_draw(command_list* /*cmd*/, pipeline pipeline) const {
  uint32_t seg_hex = 0;
  if (match_segmentation_color(seg_hex)) return true;
  if (heuristic_from_pipeline(pipeline)) return true;
  return false;
}
