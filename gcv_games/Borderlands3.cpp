// Copyright (C) 2022 Jason Bunk
#include "Borderlands3.h"
#include "gcv_utils/depth_utils.h"
#include "gcv_utils/scripted_cam_buf_templates.h"


std::string GameBorderlands3::gamename_verbose() const { return "Borderlands3"; } // hopefully continues to work with future patches via the mod lua

std::string GameBorderlands3::camera_dll_name() const { return ""; } // no dll name, it's available in the exe memory space
uint64_t GameBorderlands3::camera_dll_mem_start() const { return 0; }
GameCamDLLMatrixType GameBorderlands3::camera_dll_matrix_format() const { return GameCamDLLMatrix_allmemscanrequiredtofindscriptedcambuf; }

scriptedcam_checkbuf_funptr GameBorderlands3::get_scriptedcambuf_checkfun() const {
	return template_check_scriptedcambuf_hash<double, 13, 1>;
}
uint64_t GameBorderlands3::get_scriptedcambuf_sizebytes() const {
	return template_scriptedcambuf_sizebytes<double, 13, 1>();
}
bool GameBorderlands3::copy_scriptedcambuf_to_matrix(uint8_t* buf, uint64_t buflen, CamMatrixData& rcam, std::string& errstr) const {
	return template_copy_scriptedcambuf_extrinsic_cam2world_and_fov<double, 13, 1>(buf, buflen, rcam, true, errstr);
}

bool GameBorderlands3::can_interpret_depth_buffer() const {
	return true;
}
float GameBorderlands3::convert_to_physical_distance_depth_u64(uint64_t depthval) const {
	// const double normalizeddepth = static_cast<double>(depthval) / 4294967295.0;
	// // This game has a logarithmic depth buffer with unknown constant(s).
	// // These numbers were found by a curve fit, so are approximate,
	// // but should be pretty accurate for any depth from centimeters to kilometers
	// return 1.28 / (0.000077579959 + exp_fast_approx(354.9329993 * normalizeddepth - 83.84035513));
	uint32_t depth_as_u32 = static_cast<uint32_t>(depthval);
    float depth;
    std::memcpy(&depth, &depth_as_u32, sizeof(float));

    const float n = 0.1f;
    const float f = 10000.0f;
    const float numerator_constant = (-f * n) / (n - f);
    const float denominator_constant = n / (n - f);
    return numerator_constant / (depth - denominator_constant);
}

uint64_t GameBorderlands3::get_scriptedcambuf_triggerbytes() const
{
    // 将 double 类型的注入专用魔数转换为 8 字节的整数
    const double magic_double = 1.20040525131452021e-12;
    uint64_t magic_int;
    static_assert(sizeof(magic_double) == sizeof(magic_int));
    memcpy(&magic_int, &magic_double, sizeof(magic_int));
    return magic_int;
}


