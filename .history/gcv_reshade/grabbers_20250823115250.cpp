#include "grabbers.h"
#include "copy_texture_into_packedbuf.h"
#include <cmath>
#include <cstring>

bool grab_bgra_frame(reshade::api::command_queue* q, reshade::api::resource tex,
                     std::vector<uint8_t>& out_bgra, int& w, int& h) {
  simple_packed_buf pbuf;
  depth_tex_settings depth_cfg{};
  if (!copy_texture_image_needing_resource_barrier_into_packedbuf(
          nullptr, pbuf, q, tex, TexInterp_RGB, depth_cfg)) {
    return false;
  }
  w = (int)pbuf.width; h = (int)pbuf.height;
  const size_t row_bgra = (size_t)w * 4;
  out_bgra.resize((size_t)h * row_bgra);

  if (pbuf.pixfmt == BUF_PIX_FMT_RGBA) {
    for (int y = 0; y < h; ++y) {
      const uint8_t* src = pbuf.rowptr<uint8_t>(y);
      uint8_t* dst = out_bgra.data() + (size_t)y * row_bgra;
      for (int x = 0; x < w; ++x) {
        const uint8_t r = src[4*x+0], g = src[4*x+1], b = src[4*x+2], a = src[4*x+3];
        dst[4*x+0] = b; dst[4*x+1] = g; dst[4*x+2] = r; dst[4*x+3] = a;
      }
    }
    return true;
  } else if (pbuf.pixfmt == BUF_PIX_FMT_RGB24) {
    for (int y = 0; y < h; ++y) {
      const uint8_t* src = pbuf.rowptr<uint8_t>(y);
      uint8_t* dst = out_bgra.data() + (size_t)y * row_bgra;
      for (int x = 0; x < w; ++x) {
        const uint8_t r = src[3*x+0], g = src[3*x+1], b = src[3*x+2];
        dst[4*x+0] = b; dst[4*x+1] = g; dst[4*x+2] = r; dst[4*x+3] = 255;
      }
    }
    return true;
  } else {
    reshade::log_message(reshade::log_level::error, "grab_bgra_frame: unsupported pixfmt");
    return false;
  }
}

static inline uint8_t u8clamp_i(int v){ return (uint8_t)(v<0?0:(v>255?255:v)); }

bool grab_depth_gray8(reshade::api::command_queue* q,
                      reshade::api::resource depth_tex,
                      std::vector<uint8_t>& out_gray,
                      int& w, int& h,
                      const DepthToneParams& p)
{
  simple_packed_buf pbuf;
  depth_tex_settings depth_cfg{};
  if (!copy_texture_image_needing_resource_barrier_into_packedbuf(
          nullptr, pbuf, q, depth_tex, TexInterp_Depth, depth_cfg)) {
    return false;
  }

  w = (int)pbuf.width; h = (int)pbuf.height;
  if (w<=0 || h<=0) return false;
  out_gray.resize((size_t)w * (size_t)h);

  const float alpha = (p.log_alpha > 0.f ? p.log_alpha : 1.f);
  const float denom = std::log1p(alpha);
  auto map01_farwhite_log = [&](float t01)->uint8_t {
    if (t01 < 0.f) t01 = 0.f; else if (t01 > 1.f) t01 = 1.f;
    float y = std::log1p(alpha * t01) / denom;
    int g = (int)std::lround(y * 255.0f);
    return u8clamp_i(g);
  };

  switch (pbuf.pixfmt) {
    case BUF_PIX_FMT_GRAYF32: {
      reshade::log_message(reshade::log_level::info, "grabbers: enter GRAYF32 branch");
    ...
      const float lo = p.clip_low;
      const float hi = (p.clip_high > lo ? p.clip_high : lo + 1e-6f);
      const float invspan = 1.0f / (hi - lo);
      for (int y = 0; y < h; ++y) {
        const float* src = pbuf.rowptr<float>(y);
        uint8_t* dst = out_gray.data() + (size_t)y * (size_t)w;
        for (int x = 0; x < w; ++x) {
          float d = src[x];
          if (!std::isfinite(d)) d = hi;
          float t = (d - lo) * invspan;
          dst[x] = map01_farwhite_log(t);
        }
      }
      return true;
    }
    case BUF_PIX_FMT_GRAYU32: {
      uint32_t vmin = UINT32_MAX, vmax = 0;
      for (int y = 0; y < h; ++y) {
        const uint32_t* src = pbuf.rowptr<uint32_t>(y);
        for (int x = 0; x < w; ++x) {
          uint32_t v = src[x]; if (v < vmin) vmin = v; if (v > vmax) vmax = v;
        }
      }
      const double span = (vmax > vmin) ? double(vmax - vmin) : 1.0;

      const float lo = p.clip_low;
      const float hi = (p.clip_high > lo ? p.clip_high : lo + 1e-6f);
      const float invspan = 1.0f / (hi - lo);

      for (int y = 0; y < h; ++y) {
        const uint32_t* src = pbuf.rowptr<uint32_t>(y);
        uint8_t* dst = out_gray.data() + (size_t)y * (size_t)w;
        for (int x = 0; x < w; ++x) {
          float t0 = float((double(src[x]) - double(vmin)) / span);
          float t  = (t0 - lo) * invspan;
          if (t < 0.f) t = 0.f; else if (t > 1.f) t = 1.f;
          dst[x] = map01_farwhite_log(t);
        }
      }
      return true;
    }
    default:
      reshade::log_message(reshade::log_level::error, "grab_depth_gray8: unsupported depth pixfmt");
      return false;
  }
}
