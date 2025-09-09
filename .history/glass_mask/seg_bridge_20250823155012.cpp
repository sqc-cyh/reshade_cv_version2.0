#include "seg_bridge.hpp"

// 仅在此处包含 segmentation 头，避免污染其它翻译单元
#include "segmentation/semseg_shader_register_bind.hpp"
#include "segmentation/buffer_indexing_colorization.hpp"
// 如需其它头，请继续在此处包含；不要放到别处

namespace segbridge {

// 把 segmentation 的真实类型映射到我们的 Meta 壳
// 注意：perdraw_metadata_type/colorhashfun 的命名空间按你工程实际填写
using ::perdraw_metadata_type;
using ::get_draw_metadata_including_shader_info;
using ::colorhashfun;

bool fetch_draw_meta(reshade::api::device* dev,
                     reshade::api::command_list* cmd,
                     uint32_t vertices_per_instance,
                     Meta& out_meta) {
  perdraw_metadata_type meta =
      get_draw_metadata_including_shader_info(dev, cmd, vertices_per_instance);

  static_assert(sizeof(meta) <= sizeof(out_meta.blob),
                "Increase segbridge::Meta::blob size");
  std::memcpy(out_meta.blob, &meta, sizeof(meta));
  out_meta.size = sizeof(meta);
  return true;
}

uint32_t color_from_meta(const Meta& m, uint32_t seed) {
  // 与生成 seg.png 的着色规则保持一致
  const uint32_t hex = colorhashfun(m.blob, static_cast<unsigned>(m.size), seed) & 0xFFFFFFu;
  return hex;
}

} // namespace segbridge
