#pragma once
#include <cstdint>
#include <algorithm>

namespace hud {

// BGRA draw keyboard HUD（WASD、Shift、Space）
void draw_keys_bgra(uint8_t* img, int W, int H, uint32_t keymask);

// GRAY draw keyboard HUD（用于 depth.mp4 叠加）
void draw_keys_gray(uint8_t* img, int W, int H, uint32_t keymask);

// keyboard bit mask
#ifndef KM_W
#define KM_W     (1u<<0)
#define KM_A     (1u<<1)
#define KM_S     (1u<<2)
#define KM_D     (1u<<3)
#define KM_SHIFT (1u<<4)
#define KM_SPACE (1u<<5)
#endif

} // namespace hud
