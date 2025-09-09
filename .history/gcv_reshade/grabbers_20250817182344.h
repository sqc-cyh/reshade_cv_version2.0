#pragma once
#include <vector>
#include <reshade.hpp>

// parameters from depth to grayscale
struct DepthToneParams {
  float clip_low  = 0.0f;  // Proximal truncation
  float clip_high = 1.0f;  // Distal truncation
  float log_alpha = 6.0f;  // log enhance
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
