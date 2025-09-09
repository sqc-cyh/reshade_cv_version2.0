#pragma once
#include <reshade.hpp>
#include <cstdint>
#include <memory>
#include <unordered_map>

namespace glassmask {

using namespace reshade::api;

// 先提供最小桩缓存：目前不真正创建 NOP 管线，先让工程可编译、可运行
class D3D12NopPipelineCache {
public:
    explicit D3D12NopPipelineCache(device* dev) : dev_(dev) {}
    pipeline get_or_create_nop_pipeline(pipeline orig) { return orig; } // 桩：直接返回原管线
private:
    device* dev_ = nullptr;
    std::unordered_map<uint64_t, pipeline> cache_;
};

class D3D11NopPipelineCache {
public:
    explicit D3D11NopPipelineCache(device* dev) : dev_(dev) {}
    pipeline get_or_create_nop_pipeline(pipeline orig) { return orig; } // 桩：直接返回原管线
private:
    device* dev_ = nullptr;
    std::unordered_map<uint64_t, pipeline> cache_;
};

struct NopPipelineCache {
    std::unique_ptr<D3D11NopPipelineCache> dx11;
    std::unique_ptr<D3D12NopPipelineCache> dx12;
};

} // namespace glassmask
