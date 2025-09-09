#include "glass_classifier.hpp"
#include "segmentation/semseg_shader_register_bind.hpp"
#include "segmentation/buffer_indexing_colorization.hpp"
using namespace reshade::api;

GlassClassifier::GlassClassifier(device* dev, const GlassClassifierConfig& cfg)
  : dev_(dev), cfg_(cfg) {}

// 计算当前draw的seg颜色
bool GlassClassifier::match_segmentation_color(command_list* cmd, uint32_t vertices_per_instance, uint32_t& out_hex) const {
  if (cfg_.seg_hex_colors.empty()) return false;
  // 1. 获取元数据
  perdraw_metadata_type meta = get_draw_metadata_including_shader_info(dev_, cmd, vertices_per_instance);
  // 2. 用segmentation同样的hash算法和seed算颜色
  constexpr uint32_t color_seed = 0; // 如有需要可配置
  uint32_t hex = colorhashfun(meta.data(), sizeof(perdraw_metadata_type), color_seed) & 0xFFFFFF;
  out_hex = hex;
  return (cfg_.seg_hex_colors.count(hex) > 0);
}

// 启发式：blend开且深度写关
bool GlassClassifier::heuristic_from_pipeline(pipeline p) const {
  if (!cfg_.use_heuristic) return false;
  if (p.handle == 0) return false;
  pipeline_desc desc = {};
  if (!dev_->get_pipeline_desc(p, &desc)) return false;
  bool blend_on = false;
  for (uint32_t i = 0; i < desc.graphics.blend_state.num_attachments; ++i) {
    if (desc.graphics.blend_state.attachments[i].blend_enable) { blend_on = true; break; }
  }
  bool depth_write_off = !desc.graphics.depth_stencil_state.depth_write_mask;
  return blend_on && depth_write_off;
}

// 主判断接口
bool GlassClassifier::is_glass_draw(command_list* cmd, pipeline pipeline, uint32_t vertices_per_instance) const {
  uint32_t seg_hex = 0;
  if (match_segmentation_color(cmd, vertices_per_instance, seg_hex)) return true;
  if (heuristic_from_pipeline(pipeline)) return true;
  return false;
}