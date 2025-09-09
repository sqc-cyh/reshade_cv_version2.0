#include "glassmask.hpp"
#include <cassert>
using namespace reshade::api;

static GlassMaskSystem* g_sys = nullptr;

GlassMaskSystem::GlassMaskSystem(device* dev, GlassClassifierConfig cfg)
  : dev_(dev), cfg_(std::move(cfg)) {
  clf_ = std::make_unique<GlassClassifier>(dev_, cfg_);
#if RESHADE_ADDON
  if (dev_->get_api() == device_api::d3d12)
    nop_cache_.dx12 = std::make_unique<D3D12NopPipelineCache>(dev_);
  else if (dev_->get_api() == device_api::d3d11)
    nop_cache_.dx11 = std::make_unique<D3D11NopPipelineCache>(dev_);
#endif
}
GlassMaskSystem::~GlassMaskSystem(){ uninstall(); }

static void on_bind_pipeline_cb(reshade::api::command_list* cmd, reshade::api::pipeline_stage stages, reshade::api::pipeline p) {
    if (g_sys) g_sys->on_bind_pipeline(cmd, stages, p);
}
static void on_draw_cb(reshade::api::command_list* cmd, reshade::api::primitive_topology tp, uint32_t vtx, uint32_t inst, uint32_t fv, uint32_t fi) {
    if (g_sys) g_sys->on_draw(cmd, tp, vtx, inst, fv, fi);
}
static void on_draw_indexed_cb(reshade::api::command_list* cmd, reshade::api::primitive_topology tp, uint32_t idx, uint32_t inst, uint32_t fidx, int32_t voff, uint32_t fi) {
    if (g_sys) g_sys->on_draw_indexed(cmd, tp, idx, inst, fidx, voff, fi);
}
static void on_finish_effects_cb(reshade::api::effect_runtime* runtime, reshade::api::command_list* cmd_list, reshade::api::resource src, reshade::api::resource_view) {
    if (g_sys) g_sys->on_finish_effects(runtime, cmd_list, src);
}
void GlassMaskSystem::install() {
    if (g_sys) return;
    g_sys = this;
    reshade::register_event<reshade::addon_event::bind_pipeline>(&on_bind_pipeline_cb);
    reshade::register_event<reshade::addon_event::draw>(&on_draw_cb);
    reshade::register_event<reshade::addon_event::draw_indexed>(&on_draw_indexed_cb);
    reshade::register_event<reshade::addon_event::reshade_finish_effects>(&on_finish_effects_cb);
}

// 3. 注销事件时也传同一个指针
void GlassMaskSystem::uninstall() {
    if (g_sys != this) return;
    reshade::unregister_event<reshade::addon_event::bind_pipeline>(&on_bind_pipeline_cb);
    reshade::unregister_event<reshade::addon_event::draw>(&on_draw_cb);
    reshade::unregister_event<reshade::addon_event::draw_indexed>(&on_draw_indexed_cb);
    reshade::unregister_event<reshade::addon_event::reshade_finish_effects>(&on_finish_effects_cb);
    g_sys = nullptr;
}

bool GlassMaskSystem::should_suppress_current_draw(command_list* cmd, pipeline p, uint32_t vertices_per_instance) const {
  if (!enabled_ || p.handle == 0) return false;
  return clf_->is_glass_draw(cmd, p, vertices_per_instance);
}

void GlassMaskSystem::on_bind_pipeline(command_list* /*cmd*/, pipeline_stage stages, pipeline p) {
  if (stages != pipeline_stage::all) return;
  current_pipeline_ = p;
}

static void replace_with_nop(command_list* cmd, pipeline orig,
                             NopPipelineCache& cache, device_api api,
                             bool& replaced) {
  if (api == device_api::d3d12) {
    auto nop = cache.dx12->get_or_create_nop_pipeline(orig);
    if (nop.handle) { cmd->bind_pipeline(pipeline_stage::all, nop); replaced = true; }
  } else if (api == device_api::d3d11) {
    auto nop = cache.dx11->get_or_create_nop_pipeline(orig);
    if (nop.handle) { cmd->bind_pipeline(pipeline_stage::all, nop); replaced = true; }
  }
}

void GlassMaskSystem::on_draw(command_list* cmd,
                              uint32_t vtx, uint32_t inst,
                              uint32_t fv, uint32_t fi) {
  replaced_ = false;
  if (should_suppress_current_draw(cmd, current_pipeline_, vtx)) {
    replace_with_nop(cmd, current_pipeline_, nop_cache_, dev_->get_api(), replaced_);
  }
  cmd->draw(vtx, inst, fv, fi);                 // 无 topology，4 参
  if (replaced_) { cmd->bind_pipeline(pipeline_stage::all, current_pipeline_); replaced_ = false; }
}

void GlassMaskSystem::on_draw_indexed(command_list* cmd,
                                      uint32_t idx, uint32_t inst,
                                      uint32_t fidx, int32_t voff, uint32_t fi) {
  replaced_ = false;
  if (should_suppress_current_draw(cmd, current_pipeline_, idx)) {
    replace_with_nop(cmd, current_pipeline_, nop_cache_, dev_->get_api(), replaced_);
  }
  cmd->draw_indexed(idx, inst, fidx, voff, fi); // 无 topology，5 参
  if (replaced_) { cmd->bind_pipeline(pipeline_stage::all, current_pipeline_); replaced_ = false; }
}