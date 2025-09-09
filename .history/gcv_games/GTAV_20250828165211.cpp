// GTA5 (Enhanced) adapter implementation

#include "GTAV.h"
#include "gcv_utils/depth_utils.h"
#include "gcv_utils/scripted_cam_buf_templates.h"
#include <algorithm>

// ---- scripted camera buffer (cam->world + FOV) ----
// We follow the same template pattern as Cyberpunk/Witcher3, but with double[13].

scriptedcam_checkbuf_funptr GameGTAV::get_scriptedcambuf_checkfun() const {
    // hash-based validator for a contiguous scripted buffer of length 13 (double)
    return template_check_scriptedcambuf_hash<double, 13, 1>;
}

uint64_t GameGTAV::get_scriptedcambuf_sizebytes() const {
    return template_scriptedcambuf_sizebytes<double, 13, 1>();
}

bool GameGTAV::copy_scriptedcambuf_to_matrix(uint8_t* buf, uint64_t buflen,
                                             CamMatrixData& rcam, std::string& errstr) const {
    // copy cam->world extrinsic and FOV from the scripted buffer into rcam
    // The last 'true' indicates the extrinsic is cam-to-world
    return template_copy_scriptedcambuf_extrinsic_cam2world_and_fov<double, 13, 1>(
        buf, buflen, rcam, /*cam2world=*/true, errstr
    );
}

// ---- depth interpretation ----
//
// Start with a standard D3D11-style perspective inverse mapping from normalized depth d in [0,1]
// to view-space Z (meters). Near/far should match the in-game camera (read/written by your SHVDN plugin).
// You can refine these via calibration if GTA5 uses a non-standard depth mapping in your setup.

static inline float gtav_inverse_perspective_z(float d, float n, float f)
{
    // Clamp d to a valid range to avoid division by zero at exactly 0/1
    const double dd = std::clamp(static_cast<double>(d), 0.0, 1.0);
    // z_view = (2 n f) / (f + n - (2 d - 1) (f - n))
    const double num = 2.0 * n * f;
    const double den = (f + n) - (2.0 * dd - 1.0) * (f - n);
    return static_cast<float>(num / std::max(1e-12, den));
}

bool GameGTAV::can_interpret_depth_buffer() const {
    return true;
}

float GameGTAV::convert_to_physical_distance_depth_u64(uint64_t depthval) const
{
    // Assume 32-bit normalized depth coming from the resolved depth surface (R32F normalized).
    // If your capture path packs differently, modify the divisor accordingly.
    const double d = static_cast<double>(depthval) / 4294967295.0;
    // const float d = *reinterpret_cast<const float*>(&depthval);

    // Default near/far (meters). Prefer to keep these synchronized with your SHVDN writer:
    // e.g., write NearClip/FarClip into a small shared buffer if you need exact values.
    constexpr float NEAR_CLIP = 0.15f;
    constexpr float FAR_CLIP  = 10003.814f;

    // Return view-space Z as a proxy for physical distance. If you need Euclidean range, project using ray direction.s
    return gtav_inverse_perspective_z(static_cast<float>(d), NEAR_CLIP, FAR_CLIP);
}
