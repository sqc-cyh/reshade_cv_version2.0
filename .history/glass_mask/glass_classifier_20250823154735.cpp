#include "glass_classifier.hpp"
#include <cstdint>

// ===== 开关：是否启用 segmentation 的真实实现 =====
#ifndef GLASSMASK_USE_SEG
#define GLASSMASK_USE_SEG 1
#endif

#if GLASSMASK_USE_SEG
  // 按你工程的实际包含路径调整
  #include "segmentation/semseg_shader_register_bind.hpp"
  #include "segmentation/buffer_indexing_colorization.hpp"
#endif

namespace glassmask {

using namespace reshade::api;

#if !GLASSMASK_USE_SEG
// ---- 占位：简易 FNV-1a 哈希，方便在未接入 segmentation 时先联通流程 ----
static inline uint32_t fnv1a32(const void* data, size_t n, uint32_t seed = 2166136261u) {
  const uint8_t* p = static_cast<const uint8_t*>(data);
  uint32_t h = seed;
  for (size_t i = 0; i < n; ++i) { h ^= p[i]; h *= 16777619u; }
  return h;
}
#endif

// =============== 需要你替换的两个函数：BEGIN ===============

bool GlassClassifier::match_segmentation_color(command_list* cmd,
                                               uint32_t vertices_per_instance,
                                               uint32_t& out_hex) const {
  if (cfg_.seg_hex_colors.empty()) return false;

#if GLASSMASK_USE_SEG
  // 1) 取 per-draw 元数据（函数名/类型按你的 segmentation 实现为准）
  //    如果你的类型/函数处于命名空间（例如 segmentation::），请加上命名空间限定。
  perdraw_metadata_type meta =
      get_draw_metadata_including_shader_info(dev_, cmd, vertices_per_instance);

  // 2) 与 seg.png 使用的同一上色规则得到 RGB24
  const uint32_t hex = colorhashfun(&meta, sizeof(meta), 0) & 0xFFFFFFu;

  out_hex = hex;
  return (cfg_.seg_hex_colors.count(hex) != 0);

#else
  // 未接入 segmentation 的占位实现：用 (cmd 指针 + 顶点/索引数) 生成稳定哈希
  struct StubMD { uint64_t cmdptr; uint32_t vtx; } md {
    reinterpret_cast<uint64_t>(cmd), vertices_per_instance
  };
  const uint32_t hex = fnv1a32(&md, sizeof(md)) & 0xFFFFFFu;

  out_hex = hex;
  return (cfg_.seg_hex_colors.count(hex) != 0);
#endif
}

bool GlassClassifier::is_glass_draw(command_list* cmd,
                                    pipeline current_pipeline,
                                    uint32_t vertices_per_instance) const {
  uint32_t seg_hex = 0;
  if (match_segmentation_color(cmd, vertices_per_instance, seg_hex))
    return true;

  if (heuristic_from_pipeline(current_pipeline))
    return true;

  return false;
}

// =============== 需要你替换的两个函数：END ===============

} // namespace glassmask
