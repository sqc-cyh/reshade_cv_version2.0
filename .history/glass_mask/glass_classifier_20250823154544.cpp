#include "glass_classifier.hpp"
#include <cstring>
#include <cstdint>

// ===== 选项：是否启用 segmentation 真实现 =====
#ifndef GLASSMASK_USE_SEG
#define GLASSMASK_USE_SEG 1
#endif

#if GLASSMASK_USE_SEG
  // 仅当你已把 segmentation 的所有头/库正确纳入本工程时再启用
  #include "segmentation/semseg_shader_register_bind.hpp"
  #include "segmentation/buffer_indexing_colorization.hpp"
#endif

namespace glassmask {
using namespace reshade::api;

#if !GLASSMASK_USE_SEG
// --------- 占位：FNV-1a 哈希，避免链接错误 ---------
static inline uint32_t fnv1a32(const void* data, size_t n, uint32_t seed=2166136261u) {
    const uint8_t* p = static_cast<const uint8_t*>(data);
    uint32_t h = seed;
    for (size_t i=0;i<n;++i) { h ^= p[i]; h *= 16777619u; }
    return h;
}
#endif

bool GlassClassifier::match_segmentation_color(command_list* cmd,
                                               uint32_t vertices_per_instance,
                                               uint32_t& out_hex) const
{
    if (cfg_.seg_hex_colors.empty())
        return false;

#if GLASSMASK_USE_SEG
    // —— 真实实现（等 segmentation 依赖接入后启用）——
    // perdraw_metadata_type meta =
    //     get_draw_metadata_including_shader_info(dev_, cmd, vertices_per_instance);
    // const uint32_t hex = colorhashfun(&meta, sizeof(meta), 0) & 0xFFFFFFu;
    // out_hex = hex;
    // return (cfg_.seg_hex_colors.count(hex) != 0);
    return false; // 临时占位
#else
    // —— 占位实现：用 (cmd 指针, 顶点数) 生成稳定哈希，便于先联通全流程 —— 
    struct StubMD { uint64_t cmdptr; uint32_t vtx; } md {
        reinterpret_cast<uint64_t>(cmd), vertices_per_instance
    };
    uint32_t hex = fnv1a32(&md, sizeof(md)) & 0xFFFFFFu;
    out_hex = hex;
    return (cfg_.seg_hex_colors.count(hex) != 0);
#endif
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
