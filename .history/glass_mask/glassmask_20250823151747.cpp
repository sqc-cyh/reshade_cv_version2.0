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

void GlassMaskSystem::install() {
  if (g_sys) return;
  g_sys = this;

  reshade::register_event<reshade::addon_event::bind_pipeline>(
    +[](command_list* cmd, pipeline_stage stages, pipeline p) {
      if (g_sys) g_sys->on_bind_pipeline(cmd, stages, p);
    });

  reshade::register_event<reshade::addon_event::draw>(
    +[](command_list* cmd, primitive_topology tp,
        uint32_t vtx, uint32_t inst, uint32_t fv, uint32_t fi) {
      if (g_sys) g_sys->on_draw(cmd, tp, vtx, inst, fv, fi);
    });

  reshade::register_event<reshade::addon_event::draw_indexed>(
    +[](command_list* cmd, primitive_topology tp,
        uint32_t idx, uint32_t inst, uint32_t fidx, int32_t voff, uint32_t fi) {
      if (g_sys) g_sys->on_draw_indexed(cmd, tp, idx, inst, fidx, voff, fi);
    });

  // 注册 reshade_finish_effects 事件，把backbuffer拷贝到自定义纹理
  reshade::register_event<reshade::addon_event::reshade_finish_effects>(
    +[](effect_runtime* runtime, command_list* cmd_list, resource_view rtv, resource_view) {
      device* dev = runtime->get_device();
      resource src = dev->get_resource_from_view(rtv);

      // 创建纹理和SRV（只需一次）
      if (!g_glassmask_tex.handle) {
        resource_desc desc = dev->get_resource_desc(src);
        desc.type = resource_type::texture_2d;
        desc.heap = memory_heap::gpu_only;
        desc.usage = resource_usage::shader_resource | resource_usage::copy_dest;
        desc.format = format::r8g8b8a8_unorm;
        g_glassmask_tex = dev->create_resource(desc, nullptr);
        g_glassmask_srv = dev->create_resource_view(g_glassmask_tex, resource_usage::shader_resource, format::r8g8b8a8_unorm);
      }
      // 拷贝backbuffer到glassmask纹理
      cmd_list->copy_texture_region(src, 0, nullptr, g_glassmask_tex, 0, nullptr);

      // 绑定给glassmask.fx
      runtime->update_texture_binding("glassmask.fx", "GlassMaskTex", g_glassmask_srv);
    });
}

void GlassMaskSystem::uninstall() {
  if (g_sys != this) return;
  reshade::unregister_event<reshade::addon_event::bind_pipeline>();
  reshade::unregister_event<reshade::addon_event::draw>();
  reshade::unregister_event<reshade::addon_event::draw_indexed>();
  reshade::unregister_event<reshade::addon_event::reshade_finish_effects>();
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