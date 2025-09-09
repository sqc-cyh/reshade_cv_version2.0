#include <reshade.hpp>
#include "glassmask_app_data.hpp"
using namespace reshade::api;

static void on_device_init(device* device) {
    device->create_private_data<glassmask_app_data>();
}
static void on_device_destroy(device* device) {
    auto& mapp = device->get_private_data<glassmask_app_data>();
    mapp.glassmask_tex.delete_resource(device);
    device->destroy_private_data<glassmask_app_data>();
}

static void on_draw(command_list* cmd_list, uint32_t vertices, uint32_t instances, uint32_t first_vertex, uint32_t first_instance) {
    auto& mapp = cmd_list->get_device()->get_private_data<glassmask_app_data>();
    // 获取当前PS hash（仿照 segmentation 里的做法）
    uint64_t curr_ps_hash = 0;
    // TODO: 填写获取当前 draw call 的 pixel shader hash 的逻辑
    // curr_ps_hash = ...

    if (curr_ps_hash == mapp.glass_ps_hash) {
        // 拦截玻璃draw call，写入mask
        // 获取分辨率
        resource backbuffer = cmd_list->get_device()->get_swapchain(0)->get_current_back_buffer();
        resource_desc desc = cmd_list->get_device()->get_resource_desc(backbuffer);
        mapp.glassmask_tex.create_or_resize_texture(cmd_list->get_device(), desc.texture.width, desc.texture.height);

        // 绑定mask为RTV并清空
        cmd_list->bind_render_targets(1, &mapp.glassmask_tex.rtv, nullptr);
        float clear_value[4] = {0, 0, 0, 0};
        cmd_list->clear_render_target_view(mapp.glassmask_tex.rtv, clear_value);

        // 绘制全屏quad或当前几何体，输出1到mask
        // TODO: 你可以patch玻璃shader，强制输出1到R8 render target
    }
}

void register_glassmask_hooks() {
    reshade::register_event<reshade::addon_event::init_device>(on_device_init);
    reshade::register_event<reshade::addon_event::destroy_device>(on_device_destroy);
    reshade::register_event<reshade::addon_event::draw>(on_draw);
}

void unregister_glassmask_hooks() {
    reshade::unregister_event<reshade::addon_event::init_device>(on_device_init);
    reshade::unregister_event<reshade::addon_event::destroy_device>(on_device_destroy);
    reshade::unregister_event<reshade::addon_event::draw>(on_draw);
}