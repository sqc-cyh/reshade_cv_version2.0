#include "MicrosoftFlightSimulator2020.h"

#include "gcv_utils/depth_utils.h"
#include "gcv_utils/scripted_cam_buf_templates.h"

std::string GameMicrosoftFlightSimulator2020::gamename_verbose() const { return "Microsoft Flight Simulator 2020"; }

std::string GameMicrosoftFlightSimulator2020::camera_dll_name() const { return ""; }
uint64_t GameMicrosoftFlightSimulator2020::camera_dll_mem_start() const { return 0; }
GameCamDLLMatrixType GameMicrosoftFlightSimulator2020::camera_dll_matrix_format() const {
    return GameCamDLLMatrix_allmemscanrequiredtofindscriptedcambuf;
}

scriptedcam_checkbuf_funptr GameMicrosoftFlightSimulator2020::get_scriptedcambuf_checkfun() const {
    return template_check_scriptedcambuf_hash<double, 13, 1>;
}

uint64_t GameMicrosoftFlightSimulator2020::get_scriptedcambuf_sizebytes() const {
    return template_scriptedcambuf_sizebytes<double, 13, 1>();
}

bool GameMicrosoftFlightSimulator2020::copy_scriptedcambuf_to_matrix(uint8_t* buf, uint64_t buflen, CamMatrixData& rcam, std::string& errstr) const {
    return template_copy_scriptedcambuf_extrinsic_cam2world_and_fov<double, 13, 1>(buf, buflen, rcam, true, errstr);
}

bool GameMicrosoftFlightSimulator2020::can_interpret_depth_buffer() const {
    return true;
}

float GameMicrosoftFlightSimulator2020::convert_to_physical_distance_depth_u64(uint64_t depthval) const {
    float normalized_depth = 1.0f - static_cast<float>(depthval) / 4294967295.0;
    const float z_near = 1.0f;
    const float z_far = 50000.0f;
    const float linear_depth = z_near * z_far / (z_far - normalized_depth * (z_far - z_near));

    return linear_depth;
}
uint64_t GameMicrosoftFlightSimulator2020::get_scriptedcambuf_triggerbytes() const {
    const double magic_double = 1.20040525131452021e-12;
    uint64_t magic_int;
    static_assert(sizeof(magic_double) == sizeof(magic_int));
    memcpy(&magic_int, &magic_double, sizeof(magic_int));
    return magic_int;
}
