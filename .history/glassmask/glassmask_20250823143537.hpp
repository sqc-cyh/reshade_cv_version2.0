#pragma once
#include <reshade.hpp>
#include <memory>
#include <unordered_set>

struct GlassClassifierConfig {
  // 允许通过 segmentation 提供的颜色精确匹配（0xRRGGBB）
  std::unordered_set<uint32_t> seg_hex_colors;
  // 启发式：blend 开、深度写关 → 认为是“透明材质”
  bool use_heuristic = true;
};

class D3D12NopPipelineCache; // fwd
class D3D11NopPipelineCache; // fwd

// 对 DX11/DX12 统一的描述
struct NopPipelineCache {
  std::unique_ptr<D3D11NopPipelineCache> dx11;
  std::unique_ptr<D3D12NopPipelineCache> dx12;
};

class GlassClassifier; // fwd

class GlassMaskSystem {
public:
  explicit GlassMaskSystem(reshade::api::device* dev, GlassClassifierConfig cfg);
  ~GlassMaskSystem();

  void install();   // 注册回调
  void uninstall(); // 注销回调
  void set_enabled(bool e) { enabled_ = e; }

private:
  bool should_suppress_current_draw(reshade::api::command_list* cmd,
                                    reshade::api::pipeline pipeline) const;

  void on_bind_pipeline(reshade::api::command_list* cmd,
                        reshade::api::pipeline_stage stages,
                        reshade::api::pipeline pipeline);
  void on_draw(reshade::api::command_list* cmd,
               reshade::api::primitive_topology topology,
               uint32_t vtx_count, uint32_t inst_count,
               uint32_t first_vtx, uint32_t first_inst);
  void on_draw_indexed(reshade::api::command_list* cmd,
                       reshade::api::primitive_topology topology,
                       uint32_t idx_count, uint32_t inst_count,
                       uint32_t first_idx, int32_t vtx_offset, uint32_t first_inst);

private:
  reshade::api::device* dev_ = nullptr;
  GlassClassifierConfig cfg_;
  bool enabled_ = true;

  std::unique_ptr<GlassClassifier> clf_;
  NopPipelineCache nop_cache_;

  // 每次 draw 前暂存原 pipeline，draw 后恢复
  reshade::api::pipeline current_pipeline_ = { 0 };
  bool replaced_ = false;
};
