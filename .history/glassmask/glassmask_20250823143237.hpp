#pragma once
#include <reshade.hpp>
#include "glass_classifier.hpp"
#include "d3d_pipeline_nop_cache.hpp"

class GlassMaskSystem {
public:
  explicit GlassMaskSystem(reshade::api::device* dev,
                           const GlassClassifierConfig& cfg);

  // 注册/注销事件
  void install();
  void uninstall();

  // 可选：切换开关（ImGui/热键）
  void set_enabled(bool v) { enabled_ = v; }
  bool enabled() const { return enabled_; }

private:
  reshade::api::device* device_ = nullptr;
  PipelineNopCache pso_cache_;
  GlassClassifier classifier_;
  bool enabled_ = true; // 默认开启

  // —— 事件回调 —— //
  void on_bind_pipeline(reshade::api::command_list* cmd, reshade::api::pipeline pso);
  void on_draw(reshade::api::command_list* cmd, uint32_t vertices, uint32_t instances, uint32_t start_vertex, uint32_t start_instance);
  void on_draw_indexed(reshade::api::command_list* cmd, uint32_t indices, uint32_t instances, uint32_t start_index, int32_t base_vertex, uint32_t start_instance);

  // 每个 command_list 的“当前 PSO 与是否需置换”的轻量状态
  struct CLState {
    reshade::api::pipeline cur_pso{};
    bool should_replace = false;
  };
  // 用哈希表跟踪各 command_list
  std::unordered_map<uint64_t, CLState> cl_states_;

  CLState& state_of(reshade::api::command_list* cmd) {
    return cl_states_[cmd->handle];
  }
};
