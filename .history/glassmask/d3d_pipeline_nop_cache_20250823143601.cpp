#include "d3d_pipeline_nop_cache.hpp"
using namespace reshade::api;

static uint64_t pipeline_key(const pipeline_desc& d) {
  // 粗略哈希：取 VS/PS/DS/HS/GS 指针与 RT 格式等（足够区分）
  uint64_t h = 1469598103934665603ull;
  auto mix=[&](uint64_t v){ h ^= v; h *= 1099511628211ull; };
  mix(reinterpret_cast<uint64_t>(d.graphics.vertex_shader.handle));
  mix(reinterpret_cast<uint64_t>(d.graphics.pixel_shader.handle));
  mix(reinterpret_cast<uint64_t>(d.graphics.domain_shader.handle));
  mix(reinterpret_cast<uint64_t>(d.graphics.hull_shader.handle));
  mix(reinterpret_cast<uint64_t>(d.graphics.geometry_shader.handle));
  mix(static_cast<uint64_t>(d.graphics.num_render_targets));
  for (uint32_t i=0;i<d.graphics.num_render_targets;i++)
    mix(static_cast<uint64_t>(d.graphics.render_target_formats[i]));
  mix(static_cast<uint64_t>(d.graphics.depth_stencil_format));
  return h;
}

static void make_rt_write_mask_zero(pipeline_desc& d) {
  for (uint32_t i = 0; i < d.graphics.blend_state.num_attachments; ++i) {
    d.graphics.blend_state.attachments[i].render_target_write_mask = 0; // 不写颜色
  }
  // 深度/模板保持原状（不改变排序/遮挡关系）
}

pipeline D3D12NopPipelineCache::get_or_create_nop_pipeline(pipeline orig) {
  if (!orig.handle) return {0};
  pipeline_desc desc{};
  if (!dev_->get_pipeline_desc(orig, &desc)) return {0};

  pipeline_desc nop = desc;
  make_rt_write_mask_zero(nop);
  const uint64_t key = pipeline_key(nop);

  auto it = cache_.find(key);
  if (it != cache_.end()) return it->second;

  pipeline created{};
  if (dev_->create_pipeline(nop, &created)) {
    cache_[key] = created;
    return created;
  }
  return {0};
}

pipeline D3D11NopPipelineCache::get_or_create_nop_pipeline(pipeline orig) {
  if (!orig.handle) return {0};
  pipeline_desc desc{};
  if (!dev_->get_pipeline_desc(orig, &desc)) return {0};

  pipeline_desc nop = desc;
  make_rt_write_mask_zero(nop);
  const uint64_t key = pipeline_key(nop);

  auto it = cache_.find(key);
  if (it != cache_.end()) return it->second;

  pipeline created{};
  if (dev_->create_pipeline(nop, &created)) {
    cache_[key] = created;
    return created;
  }
  return {0};
}
