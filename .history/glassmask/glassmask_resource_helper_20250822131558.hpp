#pragma once
#include <reshade.hpp>
using namespace reshade::api;

// 工具：把 backbuffer 拷到我们的纹理（尺寸、格式已对齐）
inline void copy_texture(device *dev, command_list *cmd, resource src, resource dst)
{
    resource_desc sdesc = dev->get_resource_desc(src);
    resource_desc ddesc = dev->get_resource_desc(dst);

    subresource_box box = { 0, 0, 0, (int32_t)sdesc.texture.width, (int32_t)sdesc.texture.height, 1 };
    cmd->barrier(src, resource_usage::present, resource_usage::copy_source);
    cmd->barrier(dst, resource_usage::shader_resource, resource_usage::copy_dest);
    cmd->copy_texture_region(src, 0, nullptr, dst, 0, &box);
    cmd->barrier(dst, resource_usage::copy_dest, resource_usage::shader_resource);
    cmd->barrier(src, resource_usage::copy_source, resource_usage::present);
}

// 简单 hash（如需稳定可换 xxHash）
inline uint64_t hash_bytes(const void *data, size_t size)
{
    // FNV-1a 64-bit
    const uint8_t *p = static_cast<const uint8_t *>(data);
    uint64_t h = 1469598103934665603ull;
    for (size_t i = 0; i < size; ++i) { h ^= p[i]; h *= 1099511628211ull; }
    return h;
}

// 借助 Reshade API 取 pipeline 的 PS 字节码并计算哈希
inline uint64_t calc_ps_hash(device *dev, pipeline p)
{
    if (!p.handle) return 0;
    pipeline_desc desc = {};
    if (!dev->get_pipeline_desc(p, &desc)) return 0;
    if (desc.graphics.ps.code != nullptr && desc.graphics.ps.code_size > 0)
        return hash_bytes(desc.graphics.ps.code, desc.graphics.ps.code_size);
    return 0;
}
