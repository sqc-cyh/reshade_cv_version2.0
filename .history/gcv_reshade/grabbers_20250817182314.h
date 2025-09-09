#pragma once
#include <vector>
#include <reshade.hpp>

// parameters from depth to grayscale（替换原来的全局 g_depth_clip_* / g_depth_log_alpha）
struct DepthToneParams {
  float clip_low  = 0.0f;  // 近端截断（0..1）
  float clip_high = 1.0f;  // 远端截断（0..1）
  float log_alpha = 6.0f;  // 对数增强强度
};

// 读取 RGBA/RGB 到 BGRA（A=255），输出连续内存
bool grab_bgra_frame(reshade::api::command_queue* q,
                     reshade::api::resource color_tex,
                     std::vector<uint8_t>& out_bgra,
                     int& w, int& h);

// 读取深度纹理并映射为灰度（远白近黑，带 clip 与对数增强）
bool grab_depth_gray8(reshade::api::command_queue* q,
                      reshade::api::resource depth_tex,
                      std::vector<uint8_t>& out_gray,
                      int& w, int& h,
                      const DepthToneParams& p);
