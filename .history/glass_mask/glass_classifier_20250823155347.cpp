#pragma once
#include <reshade.hpp>
#include "glass_classifier.hpp"
#include "glass_classifier.hpp"
#include "seg_bridge.hpp"  // ← 新增
#include <cstdint>

namespace glassmask {
using namespace reshade::api;

bool GlassClassifier::match_segmentation_color(command_list* cmd,
                                               uint32_t vertices_per_instance,
                                               uint32_t& out_hex) const
{
  if (cfg_.seg_hex_colors.empty()) return false;

  segbridge::Meta meta{};
  if (!segbridge::fetch_draw_meta(dev_, cmd, vertices_per_instance, meta))
    return false;

  const uint32_t hex = segbridge::color_from_meta(meta, /*seed=*/0);
  out_hex = hex;
  return (cfg_.seg_hex_colors.count(hex) != 0);
}

bool GlassClassifier::is_glass_draw(command_list* cmd,
                                    pipeline current_pipeline,
                                    uint32_t vertices_per_instance) const
{
  uint32_t seg_hex = 0;
  if (match_segmentation_color(cmd, vertices_per_instance, seg_hex))
    return true;

  if (heuristic_from_pipeline(current_pipeline))
    return true;

  return false;
}

} // namespace glassmask
