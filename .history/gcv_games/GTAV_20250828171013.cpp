// GTA5 (Enhanced) adapter implementation (CP2077-style)
// Copyright (C)

#include "GTAV.h"
#include "gcv_utils/depth_utils.h"
#include "gcv_utils/scripted_cam_buf_templates.h"
#include <algorithm>
#include <cmath>

// Keep the same public-identification scheme as CP2077
std::string GameGTAV::gamename_verbose() const { return "Grand Theft Auto V Enhanced"; }

// --- scripted camera buffer (cam->world + FOV) ---
// We reuse the same double[13], HASH=1 template as in Cyberpunk/Witcher3.

scriptedcam_checkbuf_funptr GameGTAV::get_scriptedcambuf_checkfun() const {
    return template_check_scriptedcambuf_hash<double, 13, 1>;
}
uint64_t GameGTAV::get_scriptedcambuf_sizebytes() const {
    return template_scriptedcambuf_sizebytes<double, 13, 1>();
}
bool GameGTAV::copy_scriptedcambuf_to_matrix(uint8_t* buf, uint64_t buflen,
                                             CamMatrixData& rcam, std::string& errstr) const {
    // last boolean 'true' means the extrinsic in the buffer is cam->world
    return template_copy_scriptedcambuf_extrinsic_cam2world_and_fov<double, 13, 1>(
        buf, buflen, rcam, /*cam2world=*/true, errstr
    );
}

// --- depth interpretation ---

bool GameGTAV::can_interpret_depth_buffer() const {
    return true;
}

// Option A (default here): D3D-style linearized Z from normalized depth d∈[0,1] with near/far.
// You can later replace NEAR/FAR with your measured values, or switch to the P22/P32 form:
//   Z = P32 / (d - P22)   // if you prefer to read projection matrix terms directly.
static inline float gtav_inverse_perspective_z(float d, float n, float f)
{
    // Clamp to avoid division by zero at exact 0/1 due to numerical noise
    const double dd = std::clamp(static_cast<double>(d), 0.0, 1.0);
    const double num = 2.0 * n * f;
    const double den = (f + n) - (2.0 * dd - 1.0) * (f - n);
    return static_cast<float>(num / std::max(1e-12, den));
}

float GameGTAV::convert_to_physical_distance_depth_u64(uint64_t depthval) const
{
    // If your capture path stores R32F depth verbatim in the 64-bit slot, decode as float:
    const float d = *reinterpret_cast<const float*>(&depthval);

    // TODO: replace with your measured near/far or hook them from SHVDN.
    constexpr float NEAR_CLIP = 0.15f;
    constexpr float FAR_CLIP  = 10000.0f;

    // Return view-space Z (meters). If you need Euclidean range, multiply by ray length later.
    return gtav_inverse_perspective_z(d, NEAR_CLIP, FAR_CLIP);
}
