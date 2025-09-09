#pragma once
#include "glassmask_resource_helper.hpp"
#include <reshade.hpp>

struct __declspec(uuid("d2c1b8e1-1234-4b48-b4f2-4bac4bf87e99")) glassmask_app_data {
    glassmask_resource_helper glassmask_tex;
    bool do_intercept_draw = false;
    uint64_t glass_ps_hash = 0x123456789abcdef0; // TODO: 替换为实际玻璃shader hash
};