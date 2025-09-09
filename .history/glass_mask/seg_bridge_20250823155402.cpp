#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <cstring>
#include <cstdint>
#include <type_traits>
#include <array>
#include <vector>

#include "seg_bridge.hpp"

// 仅在本翻译单元包含 segmentation 头
#include "segmentation/semseg_shader_register_bind.hpp"
#include "segmentation/buffer_indexing_colorization.hpp"

namespace segbridge {

// 依据你工程真实命名空间做别名。若头文件里是 segmentation:: 而非 reshade_cv::segmentation，改成下行：
// namespace seg = ::segmentation;
namespace seg = ::reshade_cv::segmentation;

using seg::perdraw_metadata_type;
using seg::get_draw_metadata_including_shader_info;

static inline uint32_t seg_colorhash(const void* data, unsigned size, uint32_t seed) {
    // 如果 colorhashfun 在别的命名空间，改这里即可
    return seg::colorhashfun(data, size, seed);
}

bool fetch_draw_meta(reshade::api::device* dev,
                     reshade::api::command_list* cmd,
                     uint32_t vertices_per_instance,
                     Meta& out_meta)
{
    perdraw_metadata_type meta =
        get_draw_metadata_including_shader_info(dev, cmd, vertices_per_instance);

    static_assert(sizeof(meta) <= sizeof(out_meta.blob), "Increase segbridge::Meta::blob size");
    std::memcpy(out_meta.blob, &meta, sizeof(meta));
    out_meta.size = sizeof(meta);
    return true;
}

uint32_t color_from_meta(const Meta& m, uint32_t seed) {
    const uint32_t hex = seg_colorhash(m.blob, static_cast<unsigned>(m.size), seed) & 0xFFFFFFu;
    return hex;
}

} // namespace segbridge
