#pragma once
#include <reshade.hpp>
#include <memory>
#include "glass_classifier.hpp"
#include "d3d_pipeline_nop_cache.hpp"

namespace glassmask {

using namespace reshade::api;

class GlassMaskSystem {
public:
    explicit GlassMaskSystem(device* dev, const GlassClassifierConfig& cfg);
    ~GlassMaskSystem();

    void install();
    void uninstall();
    void set_enabled(bool e) { enabled_ = e; }

    // 提供给回调包装器（必须是 public）
    void on_bind_pipeline(command_list* cmd, pipeline_stage stages, pipeline p);
    void on_draw(command_list* cmd,
                 uint32_t vertex_count, uint32_t instance_count,
                 uint32_t first_vertex, uint32_t first_instance);
    void on_draw_indexed(command_list* cmd,
                         uint32_t index_count, uint32_t instance_count,
                         uint32_t first_index, int32_t vertex_offset,
                         uint32_t first_instance);

public:
    // 内部：基于分类器决定是否跳过本次 draw（返回 true 表示“跳过原始 draw”）
    bool should_skip_draw(command_list* cmd, uint32_t vertices_per_instance);

private:
    reshade::api::device* dev_ = nullptr;
    GlassClassifierConfig cfg_{};
    bool enabled_ = true;

    std::unique_ptr<GlassClassifier> clf_;
    NopPipelineCache nop_cache_;

    pipeline current_pipeline_ = { 0 };
};

} // namespace glassmask
