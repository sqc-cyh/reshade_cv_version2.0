#pragma once
#include <reshade.hpp>
#include <unordered_map>

class PipelineNopCache {
public:
  explicit PipelineNopCache(reshade::api::device* dev) : device_(dev) {}

  // 获取或创建“无颜色写出”的 PSO 变体（保留其它状态）
  reshade::api::pipeline get_or_create_nop(reshade::api::pipeline orig) {
    auto it = cache_.find(orig.handle);
    if (it != cache_.end()) return {it->second};

    reshade::api::pipeline_desc desc{};
    if (!device_->get_pipeline_desc(orig, &desc)) return orig; // 失败则退回原始PSO

    auto& gs = desc.graphics;
    for (uint32_t i = 0; i < 8; ++i) {
      gs.blend_state.render_target[i].render_target_write_mask = 0; // 关键
      // 可选：gs.blend_state.render_target[i].blend_enable = false;
    }
    reshade::api::pipeline pso_nop{};
    if (!device_->create_pipeline(desc, &pso_nop)) return orig;

    cache_.emplace(orig.handle, pso_nop.handle);
    return pso_nop;
  }

private:
  reshade::api::device* device_ = nullptr;
  std::unordered_map<uint64_t, uint64_t> cache_;
};
