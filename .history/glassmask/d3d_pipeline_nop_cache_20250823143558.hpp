#pragma once
#include <reshade.hpp>
#include <unordered_map>

class D3D12NopPipelineCache {
public:
  explicit D3D12NopPipelineCache(reshade::api::device* dev) : dev_(dev) {}
  reshade::api::pipeline get_or_create_nop_pipeline(reshade::api::pipeline orig);
private:
  reshade::api::device* dev_;
  std::unordered_map<uint64_t, reshade::api::pipeline> cache_;
};

class D3D11NopPipelineCache {
public:
  explicit D3D11NopPipelineCache(reshade::api::device* dev) : dev_(dev) {}
  reshade::api::pipeline get_or_create_nop_pipeline(reshade::api::pipeline orig);
private:
  reshade::api::device* dev_;
  std::unordered_map<uint64_t, reshade::api::pipeline> cache_;
};
