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
    // EXACTLY the same normalization and fitted-log-depth formula as Cyberpunk2077:

    // const double normalizeddepth = static_cast<double>(depthval) / 4294967295.0;
    
    // This game has a logarithmic depth buffer with unknown constant(s).
    // These numbers were found by a curve fit, so are approximate,
    // but should be pretty accurate for any depth from centimeters to kilometers
    return static_cast<float>( 1.28 / (0.000077579959
            + exp_fast_approx(354.9329993 * normalizeddepth - 83.84035513)) );
    // const double = normalizeddepth = static_cast<double>(depthval)
    // return normalizeddepth
}
