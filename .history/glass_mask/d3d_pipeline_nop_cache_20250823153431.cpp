#include "d3d_pipeline_nop_cache.hpp"

namespace glassmask {
// 目前为桩实现，真正的 NOP 创建逻辑后续可在 create/init_pipeline 事件里
// 基于 pipeline 子对象描述克隆并把 RT write mask 置 0 再缓存
// 这里留空即可（头文件里已返回 orig）
} // namespace glassmask
