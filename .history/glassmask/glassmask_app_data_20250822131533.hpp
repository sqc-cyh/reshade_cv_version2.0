#pragma once
#include <reshade.hpp>
#include <unordered_set>
#include <unordered_map>
using namespace reshade::api;

struct tex2d_views {
    resource res = { 0 };
    resource_view rtv = { 0 };
    resource_view srv = { 0 };
    uint32_t width = 0, height = 0;
    format fmt = format::unknown;

    void destroy(device *dev) {
        if (srv.handle) dev->destroy_resource_view(srv);
        if (rtv.handle) dev->destroy_resource_view(rtv);
        if (res.handle) dev->destroy_resource(res);
        srv = {}; rtv = {}; res = {}; width = height = 0; fmt = format::unknown;
    }
    bool create_or_resize(device *dev, uint32_t w, uint32_t h, format f) {
        if (res.handle && width == w && height == h && fmt == f) return true;
        destroy(dev);
        resource_desc desc = resource_desc(resource_type::texture_2d, f, w, h, 1, 1, 1,
                                           memory_heap::gpu_only, resource_usage::render_target | resource_usage::shader_resource);
        if (!dev->create_resource(desc, nullptr, resource_heap::unknown, &res)) return false;
        if (!dev->create_resource_view(res, resource_view_desc(f), resource_usage::render_target, &rtv)) return false;
        if (!dev->create_resource_view(res, resource_view_desc(f), resource_usage::shader_resource, &srv)) return false;
        width = w; height = h; fmt = f; return true;
    }
};

struct glassmask_app_data {
    // 由 C++ 生成、给 .fx 采样的纹理
    tex2d_views scene_no_glass; // 颜色格式与 backbuffer 一致，存“第一次玻璃前”的画面

    // 本帧状态
    bool captured_this_frame = false;

    // 每个命令列表的“当前 PS 哈希”
    std::unordered_map<command_list *, uint64_t> curr_ps_hash;

    // 识别规则：玻璃 PS 的哈希白名单（可运行时加载）
    std::unordered_set<uint64_t> glass_ps_hashes;

    // 启发式开关（备选）
    bool use_heuristics = true;

    // 将本纹理绑定到 .fx 的变量名
    // （在 reshade_begin_effects 里查找该名字的纹理绑定并指向 scene_no_glass.srv）
    const char *fx_tex_binding_name = "Glass_NoGlassTex";
};
