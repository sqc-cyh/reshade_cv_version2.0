#include "glass_classifier.hpp"
#include "glassmask.hpp"
#include "segmentation_app_data.hpp"
#include "buffer_indexing_colorization.hpp" // colorhashfun
using namespace reshade::api;

GlassClassifier::GlassClassifier(device* dev, const GlassClassifierConfig& cfg)
  : dev_(dev), cfg_(cfg) {}

/// ====== 需要你接线的地方（见下文“缺少的功能”） ======
static bool query_current_draw_seg_hex(uint32_t& hex_rrggbb) {
  // 获取 segmentation 的 app data
  auto* device = reshade::api::global_device; // 或通过 cmd->get_device() 传进来
  if (!device) return false;
  auto* segdata = device->get_private_data<segmentation_app_data>();
  if (!segdata) return false;

  // 获取当前 draw 的 metadata
  const auto& perdraw_meta = segdata->r_counter_buf.perdraw_meta;
  uint32_t draw_idx = segdata->r_counter_buf.perdraw_counter - 1;
  if (draw_idx >= perdraw_meta.size()) return false;
  const perdraw_metadata_type& meta = perdraw_meta[draw_idx];

  // 计算颜色
  hex_rrggbb = colorhashfun(meta.data(), sizeof(perdraw_metadata_type), segdata->viz_seg_colorization_seed) & 0xFFFFFF;
  return true;
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
