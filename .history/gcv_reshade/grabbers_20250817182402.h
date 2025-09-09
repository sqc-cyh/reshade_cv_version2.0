#pragma once
#include <vector>
#include <reshade.hpp>

// parameters from depth to grayscale
struct DepthToneParams {
  float clip_low  = 0.0f;  // Proximal truncation
  float clip_high = 1.0f;  // Distal truncation
  float log_alpha = 6.0f;  // log enhance
};

// Read RGBA/RGB to BGRA (A=255) and output continuous memory
bool grab_bgra_frame(reshade::api::command_queue* q,
                     reshade::api::resource color_tex,
                     std::vector<uint8_t>& out_bgra,
                     int& w, int& h);

// Read the depth texture and map it to grayscale (far white, near black, with clip and logarithmic enhancement)
bool grab_depth_gray8(reshade::api::command_queue* q,
                      reshade::api::resource depth_tex,
                      std::vector<uint8_t>& out_gray,
                      int& w, int& h,
                      const DepthToneParams& p);
