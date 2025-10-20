#include "RDR2.h"
#include "gcv_utils/depth_utils.h"
#include "gcv_utils/scripted_cam_buf_templates.h"

std::string GameRDR2::gamename_verbose() const { return "Red Dead Redemption 2"; }

std::string GameRDR2::camera_dll_name() const { return ""; }

uint64_t GameRDR2::camera_dll_mem_start() const { return 0; }

GameCamDLLMatrixType GameRDR2::camera_dll_matrix_format() const {
    return GameCamDLLMatrix_allmemscanrequiredtofindscriptedcambuf;
}

scriptedcam_checkbuf_funptr GameRDR2::get_scriptedcambuf_checkfun() const {
    return template_check_scriptedcambuf_hash<double, 13, 1>;
}

uint64_t GameRDR2::get_scriptedcambuf_sizebytes() const {
    return template_scriptedcambuf_sizebytes<double, 13, 1>();
}

bool GameRDR2::copy_scriptedcambuf_to_matrix(uint8_t* buf, uint64_t buflen, CamMatrixData& rcam, std::string& errstr) const {
    return template_copy_scriptedcambuf_extrinsic_cam2world_and_fov<double, 13, 1>(
        buf, buflen, rcam, true, errstr
    );
}

bool GameRDR2::can_interpret_depth_buffer() const {
    return true;
}

float GameRDR2::convert_to_physical_distance_depth_u64(uint64_t depthval) const {

    uint32_t depth_as_u32 = static_cast<uint32_t>(depthval);
    float depth;
    std::memcpy(&depth, &depth_as_u32, sizeof(float));

    const float n = 0.15f;
    const float f = 10003.814f;
    const float numerator_constant = (-f * n) / (n - f);
    const float denominator_constant = n / (n - f);
    return numerator_constant / (depth - denominator_constant);
}
