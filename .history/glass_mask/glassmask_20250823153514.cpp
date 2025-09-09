#include "glassmask.hpp"
#include <cassert>

namespace glassmask {

using namespace reshade::api;

static GlassMaskSystem* g_sys = nullptr;

// ------------ 回调包装器（必须与 ReShade 5.8 的签名一致） ------------
static bool on_bind_pipeline_cb(command_list* cmd, pipeline_stage stages, pipeline p)
{
    if (g_sys) g_sys->on_bind_pipeline(cmd, stages, p);
    return true; // 继续后续处理
}

static bool on_draw_cb(command_list* cmd,
                       uint32_t vertex_count, uint32_t instance_count,
                       uint32_t first_vertex, uint32_t first_instance)
{
    if (g_sys && g_sys->should_skip_draw(cmd, vertex_count))
        return true; // 返回 true 表示“拦截/跳过原 draw”
    return false;    // 返回 false → 让原 draw 继续执行
}

static bool on_draw_indexed_cb(command_list* cmd,
                               uint32_t index_count, uint32_t instance_count,
                               uint32_t first_index, int32_t vertex_offset,
                               uint32_t first_instance)
{
    if (g_sys && g_sys->should_skip_draw(cmd, index_count))
        return true;
    return false;
}

// ------------ 类实现 ------------
GlassMaskSystem::GlassMaskSystem(device* dev, const GlassClassifierConfig& cfg)
    : dev_(dev), cfg_(cfg)
{
    clf_ = std::make_unique<GlassClassifier>(dev_, cfg_);

#if RESHADE_ADDON
    if (dev_->get_api() == device_api::d3d12)
        nop_cache_.dx12 = std::make_unique<D3D12NopPipelineCache>(dev_);
    else if (dev_->get_api() == device_api::d3d11)
        nop_cache_.dx11 = std::make_unique<D3D11NopPipelineCache>(dev_);
#endif
}

GlassMaskSystem::~GlassMaskSystem()
{
    uninstall();
}

void GlassMaskSystem::install()
{
    if (g_sys) return;
    g_sys = this;

    reshade::register_event<reshade::addon_event::bind_pipeline>(on_bind_pipeline_cb);
    reshade::register_event<reshade::addon_event::draw>(on_draw_cb);
    reshade::register_event<reshade::addon_event::draw_indexed>(on_draw_indexed_cb);

    // 注意：先不要注册 reshade_finish_effects（你目前不需要且旧签名会报错）
}

void GlassMaskSystem::uninstall()
{
    if (g_sys != this) return;

    reshade::unregister_event<reshade::addon_event::bind_pipeline>(on_bind_pipeline_cb);
    reshade::unregister_event<reshade::addon_event::draw>(on_draw_cb);
    reshade::unregister_event<reshade::addon_event::draw_indexed>(on_draw_indexed_cb);

    g_sys = nullptr;
}

void GlassMaskSystem::on_bind_pipeline(command_list* /*cmd*/, pipeline_stage /*stages*/, pipeline p)
{
    current_pipeline_ = p;
}

bool GlassMaskSystem::should_skip_draw(command_list* cmd, uint32_t vertices_per_instance)
{
    if (!enabled_ || !current_pipeline_.handle)
        return false;

    // 基于 segmentation 的 per-draw 颜色判定，如果命中玻璃 → 直接跳过该 drawcall
    if (clf_ && clf_->is_glass_draw(cmd, current_pipeline_, vertices_per_instance))
        return true;

    return false;
}

// 下面两个成员方法符合 5.8 原型；真正执行 draw 的动作由 ReShade 完成：
// 我们在包装器中根据 should_skip_draw 的返回值决定是否“放行”原 draw。
void GlassMaskSystem::on_draw(command_list* /*cmd*/,
                              uint32_t /*vertex_count*/, uint32_t /*instance_count*/,
                              uint32_t /*first_vertex*/, uint32_t /*first_instance*/)
{
    // 空实现：逻辑都在包装器里处理（是否跳过）
}

void GlassMaskSystem::on_draw_indexed(command_list* /*cmd*/,
                                      uint32_t /*index_count*/, uint32_t /*instance_count*/,
                                      uint32_t /*first_index*/, int32_t /*vertex_offset*/,
                                      uint32_t /*first_instance*/)
{
    // 空实现：逻辑都在包装器里处理（是否跳过）
}

} // namespace glassmask
