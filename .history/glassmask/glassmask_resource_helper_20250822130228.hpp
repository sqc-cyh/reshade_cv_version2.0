#pragma once
#include <reshade.hpp>
#include <atomic>

class glassmask_resource_helper {
protected:
    bool attemptedcreation = false;
    bool isvalid = false;
public:
    reshade::api::resource rsc = { 0 };
    reshade::api::resource_view rtv = { 0 };
    std::atomic<int> iscleared = { 0 };

    inline bool is_valid() const { return isvalid; }

    virtual void delete_resource(reshade::api::device* device) {
        if (isvalid) {
            device->destroy_resource_view(rtv);
            device->destroy_resource(rsc);
        }
        isvalid = false;
        attemptedcreation = false;
    }

    inline bool create_or_resize_texture(reshade::api::device* device, uint32_t width, uint32_t height) {
        if (isvalid) {
            reshade::api::resource_desc old = device->get_resource_desc(rsc);
            if (old.texture.width == width && old.texture.height == height) {
                return true;
            } else {
                delete_resource(device);
            }
        } else if (attemptedcreation) {
            return false;
        }
        attemptedcreation = true;

        reshade::api::resource_desc desc(width, height, 1, 1, reshade::api::format::r8_unorm, 1, reshade::api::memory_heap::gpu_only, reshade::api::resource_usage::render_target | reshade::api::resource_usage::shader_resource);

        if (!device->create_resource(desc, nullptr, reshade::api::resource_usage::render_target, &rsc)) {
            return false;
        }
        if (!device->create_resource_view(rsc, reshade::api::resource_usage::render_target, reshade::api::resource_view_desc(reshade::api::format::r8_unorm), &rtv)) {
            device->destroy_resource(rsc);
            return false;
        }
        isvalid = true;
        return isvalid;
    }
};