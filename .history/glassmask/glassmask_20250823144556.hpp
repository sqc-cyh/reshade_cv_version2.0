#pragma once
#include <reshade.hpp>
#include <memory>
#include <unordered_set>
#include "glass_classifier.hpp"
#include "d3d_pipeline_nop_cache.hpp"

class GlassMaskSystem {
public:
  explicit GlassMaskSystem(reshade::api::device* dev, GlassClassifierConfig cfg);
  ~GlassMaskSystem();

  void install();
  void uninstall();
  void set_enabled(bool e) { enabled_ = e; }

private:
  bool should_suppress_current_draw(reshade::api::command_list* cmd,
                                    reshade::api::pipeline pipeline,
                                    uint32_t vertices_per_instance) const;

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

  reshade::api::pipeline current_pipeline_ = { 0 };
  bool replaced_ = false;
};