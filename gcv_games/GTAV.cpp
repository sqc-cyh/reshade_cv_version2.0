// Copyright (C)
// GTA5 (Enhanced) adapter implemented with the EXACT SAME depth interpretation as Cyberpunk2077

#include "GTAV.h"
#include "gcv_utils/depth_utils.h"
#include "gcv_utils/scripted_cam_buf_templates.h"

// For GTA5 Enhanced, we also scan the main exe memory for the scripted camera buffer,
// matching the Cyberpunk/Witcher3 pattern (double[13], HASH=1).

std::string GameGTAV::gamename_verbose() const { return "Grand Theft Auto V"; }

std::string GameGTAV::camera_dll_name() const { return ""; } // no dll name, scan exe memory
uint64_t GameGTAV::camera_dll_mem_start() const { return 0; }
GameCamDLLMatrixType GameGTAV::camera_dll_matrix_format() const {
    return GameCamDLLMatrix_allmemscanrequiredtofindscriptedcambuf;
}

scriptedcam_checkbuf_funptr GameGTAV::get_scriptedcambuf_checkfun() const {
    return template_check_scriptedcambuf_hash<double, 13, 1>;
}
uint64_t GameGTAV::get_scriptedcambuf_sizebytes() const {
    return template_scriptedcambuf_sizebytes<double, 13, 1>();
}
bool GameGTAV::copy_scriptedcambuf_to_matrix(uint8_t* buf, uint64_t buflen,
                                             CamMatrixData& rcam, std::string& errstr) const {
    // last boolean 'true' indicates the extrinsic is cam->world
    return template_copy_scriptedcambuf_extrinsic_cam2world_and_fov<double, 13, 1>(
        buf, buflen, rcam, /*cam2world=*/true, errstr
    );
}

bool GameGTAV::can_interpret_depth_buffer() const {
    return true;
}

float GameGTAV::convert_to_physical_distance_depth_u64(uint64_t depthval) const {

    uint32_t depth_as_u32 = static_cast<uint32_t>(depthval);
    float depth;
    std::memcpy(&depth, &depth_as_u32, sizeof(float));

    const float n = 0.15f;
    const float f = 10003.814f;
    const float numerator_constant = (-f * n) / (n - f);
    const float denominator_constant = n / (n - f);
    return numerator_constant / (depth - denominator_constant);
    // 将 u64 (实际上是 u32) 的位模式重新解释为 float
    // const double normalized_depth = static_cast<double>(depthval) / static_cast<double>(std::numeric_limits<uint32_t>::max());
    // const double near_plane = 10.0;
    // const double far_plane = 100000.0;
    // const double physical_distance = (near_plane * far_plane) / (far_plane - normalized_depth * (far_plane - near_plane));

    // return static_cast<double>(physical_distance);

    // // 将深度值代入公式并返回
    // return numerator_constant / (depth - denominator_constant);


    // const float C = 0.01f;
    // const float n = 0.05f;
    // const float f = 10000.0f;
    // float d_lin = (exp(depth * log(1.0f + C)) - 1.0f) / C;
    // return (-f * n) / (d_lin * (f - n) - f);
}
