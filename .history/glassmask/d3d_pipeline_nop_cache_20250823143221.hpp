#pragma once
#include <reshade.hpp>
#include <unordered_set>
#include <cstdint>

// 你已有的状态追踪（只做前向声明，实际按你的工程包含）
// #include "command_list_state.hpp"
// #include "buffer_indexing_colorization.hpp"

struct GlassClassifierConfig {
  // 允许从 seg 颜色键匹配（RRGGBB，无#）
  std::unordered_set<uint32_t> seg_hex_colors;
  // 启用回退启发式：blend=on && depth_write=off
  bool use_heuristic = true;
};

class GlassClassifier {
public:
  explicit GlassClassifier(const GlassClassifierConfig& cfg) : cfg_(cfg) {}

  // 根据当前绑定的图形管线与资源，判断“是否玻璃”
  bool is_glass_draw(reshade::api::device* dev,
                     reshade::api::command_list* cmd,
                     const reshade::api::pipeline_desc& pso_desc
                     /*, const CommandListState& st */) const
  {
    // 1) 首选：按 segmentation 的“当前 draw 分割颜色”匹配
    if (!cfg_.seg_hex_colors.empty()) {
      uint32_t hex = 0u;
      if (compute_current_draw_seg_hex(/*st,*/ hex)) {
        if (cfg_.seg_hex_colors.count(hex)) return true;
      }
    }
    // 2) 回退：blend 开且 depth_write 关（典型透明材质）
    if (cfg_.use_heuristic) {
      const auto& gs = pso_desc.graphics;
      const bool any_blend =
        (gs.blend_state.independent_blend_enable
          ? any_target_blend_enabled(gs)
          : gs.blend_state.render_target[0].blend_enable);
      const bool depth_writes_off =
        (gs.depth_stencil_state.depth_write_mask == 0);
      if (any_blend && depth_writes_off) return true;
    }
    return false;
  }

private:
  GlassClassifierConfig cfg_;

  static bool any_target_blend_enabled(const reshade::api::pipeline_desc::graphics_desc& gs) {
    for (uint32_t i = 0; i < 8; ++i)
      if (gs.blend_state.render_target[i].blend_enable) return true;
    return false;
  }

  // === 关键占位：向 segmentation 侧索要“当前 draw 的 hex 颜色” ===
  // 请在此处调用你 segmentation 工具里**已有**的函数。
  // 例如：buffer_indexing_colorization::current_draw_hexcolor(st)
  static bool compute_current_draw_seg_hex(/*const CommandListState& st,*/ uint32_t& out_hex_rrggbb)
  {
    // TODO: 用你已有的API替换此体。
    // 返回 true 表示 out_hex_rrggbb 已写入有效 0xRRGGBB。
    (void)out_hex_rrggbb;
    return false;
  }
};
