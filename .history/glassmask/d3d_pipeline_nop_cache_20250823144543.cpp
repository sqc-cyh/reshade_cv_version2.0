#include "d3d_pipeline_nop_cache.hpp"
using namespace reshade::api;

static uint64_t pipeline_key(const pipeline_desc& d) {
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
    d.graphics.blend_state.attachments[i].render_target_write_mask = 0;
  }
}

D3D12NopPipelineCache::D3D12NopPipelineCache(device* dev) : dev_(dev) {}
D3D11NopPipelineCache::D3D11NopPipelineCache(device* dev) : dev_(dev) {}

pipeline D3D12NopPipelineCache::get_or_create_nop_pipeline(pipeline orig) {
  pipeline_desc desc = {};
  if (!dev_->get_pipeline_desc(orig, &desc)) return {0};
  make_rt_write_mask_zero(desc);
  uint64_t key = pipeline_key(desc);
  auto it = cache_.find(key);
  if (it != cache_.end()) return it->second;
  pipeline nop = dev_->create_pipeline_state(pipeline_stage::all, desc);
  cache_[key] = nop;
  return nop;
}

pipeline D3D11NopPipelineCache::get_or_create_nop_pipeline(pipeline orig) {
  pipeline_desc desc = {};
  if (!dev_->get_pipeline_desc(orig, &desc)) return {0};
  make_rt_write_mask_zero(desc);
  uint64_t key = pipeline_key(desc);
  auto it = cache_.find(key);
  if (it != cache_.end()) return it->second;
  pipeline nop = dev_->create_pipeline_state(pipeline_stage::all, desc);
  cache_[key] = nop;
  return nop;
}