#pragma once
#include <reshade.hpp>
#include <cstdint>
#include <memory>          // std::unique_ptr
#include <unordered_map>   // std::unordered_map
#include <vector>
#include <string>
using namespace reshade::api;

class D3D12NopPipelineCache;
class D3D11NopPipelineCache;

struct NopPipelineCache {
  std::unique_ptr<D3D11NopPipelineCache> dx11;
  std::unique_ptr<D3D12NopPipelineCache> dx12;
};

class D3D12NopPipelineCache {
public:
  D3D12NopPipelineCache(device* dev);
  pipeline get_or_create_nop_pipeline(pipeline orig);
private:
  device* dev_;
  std::unordered_map<uint64_t, pipeline> cache_;
};

class D3D11NopPipelineCache {
public:
  D3D11NopPipelineCache(device* dev);
  pipeline get_or_create_nop_pipeline(pipeline orig);
private:
  device* dev_;
  std::unordered_map<uint64_t, pipeline> cache_;
};