#include "glassmask.hpp"
#include <reshade.hpp>
using namespace reshade::api;

// === 注册器 ===
static GlassMaskSystem* g_sys = nullptr;

// —— ReShade 事件注册的样板（按你的工程风格放置）——
static void on_bind_pipeline_tramp(command_list* cmd, pipeline pso) {
  if (g_sys) g_sys->on_bind_pipeline(cmd, pso);
}
static void on_draw_tramp(command_list* cmd, uint32_t vertices, uint32_t instances, uint32_t start_vertex, uint32_t start_instance) {
  if (g_sys) g_sys->on_draw(cmd, vertices, instances, start_vertex, start_instance);
}
static void on_draw_indexed_tramp(command_list* cmd, uint32_t indices, uint32_t instances, uint32_t start_index, int32_t base_vertex, uint32_t start_instance) {
  if (g_sys) g_sys->on_draw_indexed(cmd, indices, instances, start_index, base_vertex, start_instance);
}

GlassMaskSystem::GlassMaskSystem(device* dev, const GlassClassifierConfig& cfg)
  : device_(dev), pso_cache_(dev), classifier_(cfg)
{}

void GlassMaskSystem::install() {
  reshade::register_event<reshade::addon_event::bind_pipeline>(on_bind_pipeline_tramp);
  reshade::register_event<reshade::addon_event::draw>(on_draw_tramp);
  reshade::register_event<reshade::addon_event::draw_indexed>(on_draw_indexed_tramp);
  g_sys = this;
}
void GlassMaskSystem::uninstall() {
  g_sys = nullptr;
  reshade::unregister_event<reshade::addon_event::bind_pipeline>(on_bind_pipeline_tramp);
  reshade::unregister_event<reshade::addon_event::draw>(on_draw_tramp);
  reshade::unregister_event<reshade::addon_event::draw_indexed>(on_draw_indexed_tramp);
}

// —— 逻辑 ——
// 思路：在 bind_pipeline 时预判是否“玻璃”，记录 should_replace；
// 在随后的 draw / draw_indexed 前，若 should_replace=true，则用 nop-pipeline 临时置换，draw 结束后恢复。

void GlassMaskSystem::on_bind_pipeline(command_list* cmd, pipeline pso) {
  if (!enabled_) { state_of(cmd).cur_pso = pso; state_of(cmd).should_replace = false; return; }

  pipeline_desc desc{};
  if (!device_->get_pipeline_desc(pso, &desc) || desc.type != pipeline_type::graphics) {
    state_of(cmd).cur_pso = pso;
    state_of(cmd).should_replace = false;
    return;
  }

  // —— 这里可加入你已有的“命令列表状态”来辅助分类 —— //
  const bool is_glass = classifier_.is_glass_draw(device_, cmd, desc /*, your_state */);

  auto& st = state_of(cmd);
  st.cur_pso = pso;
  st.should_replace = is_glass;
}

void GlassMaskSystem::on_draw(command_list* cmd, uint32_t, uint32_t, uint32_t, uint32_t) {
  auto& st = state_of(cmd);
  if (!enabled_ || !st.should_replace || st.cur_pso.handle == 0) return;

  const pipeline nop = pso_cache_.get_or_create_nop(st.cur_pso);
  if (nop.handle != st.cur_pso.handle) {
    cmd->bind_pipeline(nop);   // 临时置换为“无颜色写出”PSO
    // 绘制调用将由 ReShade 发起，我们此处只需在 draw 之后恢复
    cmd->bind_pipeline(st.cur_pso); // 立刻恢复供后续命令使用
  }
}

void GlassMaskSystem::on_draw_indexed(command_list* cmd, uint32_t, uint32_t, uint32_t, int32_t, uint32_t) {
  on_draw(cmd, 0, 0, 0, 0);
}
