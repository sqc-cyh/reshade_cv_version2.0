#include <reshade.hpp>
#include "glassmask_app_data.hpp"
#include "glassmask_resource_helper.hpp"
using namespace reshade::api;

// ========== 设备生命周期 ==========
static void on_device_init(device *dev)
{
    dev->create_private_data<glassmask_app_data>();
    auto &app = dev->get_private_data<glassmask_app_data>();

    // 这里可从磁盘加载 glass PS 哈希白名单（可选）
    // e.g., glass_ps_hashes = { 0xDEADBEEF..., ... };
}

static void on_device_destroy(device *dev)
{
    auto &app = dev->get_private_data<glassmask_app_data>();
    app.scene_no_glass.destroy(dev);
    dev->destroy_private_data<glassmask_app_data>();
}

// ========== 帧边界 ==========
static void on_present(command_queue *queue, swapchain *swap)
{
    // 每帧重置“是否已捕获”
    auto &app = swap->get_device()->get_private_data<glassmask_app_data>();
    app.captured_this_frame = false;
}

// ========== 绑定管线（记录当前 PS 哈希） ==========
static void on_bind_pipeline(command_list *cmd, pipeline_stage stages, pipeline pipe)
{
    if ((stages & pipeline_stage::pixel_shader) == pipeline_stage::none &&
        (stages & pipeline_stage::all_graphics) == pipeline_stage::none) return;

    auto &app = cmd->get_device()->get_private_data<glassmask_app_data>();
    // 直接根据当前绑定的图形管线取 PS 字节码哈希
    uint64_t ps_hash = calc_ps_hash(cmd->get_device(), pipe);
    app.curr_ps_hash[cmd] = ps_hash;
}

// ========== 启发式：判断是否像玻璃 ==========
static bool looks_like_glass_heuristics(device *dev, pipeline p)
{
    pipeline_desc desc = {};
    if (!dev->get_pipeline_desc(p, &desc)) return false;

    const auto &bs = desc.graphics.blend_state;
    bool alpha_blend =
        bs.blend_enable[0] &&
        (bs.src_color_blend_factor == blend_factor::src_alpha || bs.src_color_blend_factor == blend_factor::one) &&
        (bs.dst_color_blend_factor == blend_factor::inv_src_alpha || bs.dst_color_blend_factor == blend_factor::one);

    const auto &ds = desc.graphics.depth_stencil_state;
    bool no_depth_write = (ds.depth_write_mask == depth_write_mask::zero);

    // 轻量启发式
    return alpha_blend && no_depth_write;
}

// ========== draw 时机：第一次遇到“玻璃”→ 捕获无玻璃画面 ==========
static void on_draw(command_list *cmd, uint32_t, uint32_t, uint32_t, uint32_t)
{
    auto *dev = cmd->get_device();
    auto &app = dev->get_private_data<glassmask_app_data>();
    if (app.captured_this_frame) return;

    // 取当前 PS 哈希
    uint64_t curr_ps_hash = 0;
    if (auto it = app.curr_ps_hash.find(cmd); it != app.curr_ps_hash.end()) curr_ps_hash = it->second;

    // 尝试获取当前图形管线（用于启发式）
    pipeline current_pipe = { 0 };
    // Reshade 没有直接“get current pipeline”的接口，这里退而求其次：
    // 若你已经在 on_bind_pipeline 处缓存了“最后一次绑定的图形管线”，可以放在 app 里一并记录。
    // 为简化，此处只用哈希白名单 + 启发式（如需更稳可在 on_bind_pipeline 里顺带缓存 pipe）。

    bool is_glass = (curr_ps_hash != 0 && app.glass_ps_hashes.count(curr_ps_hash) > 0);
    if (!is_glass && app.use_heuristics) {
        // 若需要启发式，再额外判一次（需要当前 pipe；这里默认 false）
        // is_glass = looks_like_glass_heuristics(dev, current_pipe);
    }
    if (!is_glass) return;

    // 命中“玻璃”的首次 draw → 捕获 backbuffer 为“无玻璃画面”
    swapchain *sw = dev->get_swapchain(0);
    if (sw == nullptr) return;
    resource backbuf = sw->get_current_back_buffer();
    resource_desc bdesc = dev->get_resource_desc(backbuf);

    // 准备我们的纹理
    format f = bdesc.texture.format; // 与 backbuffer 一致
    if (!app.scene_no_glass.create_or_resize(dev, bdesc.texture.width, bdesc.texture.height, f)) return;

    // 拷贝
    copy_texture(dev, cmd, backbuf, app.scene_no_glass.res);
    app.captured_this_frame = true;
}

// ========== 在 .fx 开始执行前，把 SRV 绑定到指定变量 ==========
static void on_reshade_begin_effects(effect_runtime *rt, command_list *cmd, resource_view rtv, resource_view rtv_srgb)
{
    auto &app = rt->get_device()->get_private_data<glassmask_app_data>();
    if (!app.scene_no_glass.srv.handle) return;

    // 遍历并绑定纹理变量名为 app.fx_tex_binding_name 的绑定点
    // 注意：不同版本 ReShade API 的枚举/绑定函数名略有差异，如有需要请对照你当前的 ReShade 版本文档微调
    bool bound = false;
    rt->enumerate_texture_bindings(nullptr, [&](effect_runtime *, const char *semantic, resource_view &srv) {
        if (semantic && strcmp(semantic, app.fx_tex_binding_name) == 0) {
            srv = app.scene_no_glass.srv;
            bound = true;
        }
    });
    // 如果你的 ReShade 版本是使用 “name” 而非 “semantic”，可改成 enumerate_texture_variables / get_texture_binding_by_name 再 set
}

// ========== 注册/反注册 ==========
void register_glassmask_hooks()
{
    reshade::register_event<reshade::addon_event::init_device>(on_device_init);
    reshade::register_event<reshade::addon_event::destroy_device>(on_device_destroy);
    reshade::register_event<reshade::addon_event::present>(on_present);
    reshade::register_event<reshade::addon_event::bind_pipeline>(on_bind_pipeline);
    reshade::register_event<reshade::addon_event::draw>(on_draw);
    reshade::register_event<reshade::addon_event::reshade_begin_effects>(on_reshade_begin_effects);
}

void unregister_glassmask_hooks()
{
    reshade::unregister_event<reshade::addon_event::reshade_begin_effects>(on_reshade_begin_effects);
    reshade::unregister_event<reshade::addon_event::draw>(on_draw);
    reshade::unregister_event<reshade::addon_event::bind_pipeline>(on_bind_pipeline);
    reshade::unregister_event<reshade::addon_event::present>(on_present);
    reshade::unregister_event<reshade::addon_event::destroy_device>(on_device_destroy);
    reshade::unregister_event<reshade::addon_event::init_device>(on_device_init);
}
