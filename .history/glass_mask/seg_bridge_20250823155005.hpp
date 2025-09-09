#pragma once
#include <reshade.hpp>
#include <cstdint>

namespace segbridge {
  // 与 segmentation 内部类型解耦的最小“元数据”壳
  struct Meta {
    // 这里不放真实字段；只是一个占位符，供哈希函数接收
    // 真实获取过程在 seg_bridge.cpp 里完成
    alignas(8) unsigned char blob[64];
    size_t size = 0;
  };

  // 获取当前 draw 的 per-draw 元数据（封装 segmentation 的实现）
  bool fetch_draw_meta(reshade::api::device* dev,
                       reshade::api::command_list* cmd,
                       uint32_t vertices_per_instance,
                       Meta& out_meta);

  // 与 seg.png 一致的颜色哈希（封装 segmentation 的 colorhashfun 或等价逻辑）
  uint32_t color_from_meta(const Meta& m, uint32_t seed = 0);
}
