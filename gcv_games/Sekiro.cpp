// Copyright (C) 2022 Jason Bunk
#include "Sekiro.h"
#include "gcv_utils/depth_utils.h"
#include "gcv_utils/scripted_cam_buf_templates.h"


std::string GameSekiro::gamename_verbose() const { return "Sekiro"; } // hopefully continues to work with future patches via the mod lua

std::string GameSekiro::camera_dll_name() const { return ""; } // no dll name, it's available in the exe memory space
uint64_t GameSekiro::camera_dll_mem_start() const { return 0; }
GameCamDLLMatrixType GameSekiro::camera_dll_matrix_format() const { return GameCamDLLMatrix_allmemscanrequiredtofindscriptedcambuf; }

scriptedcam_checkbuf_funptr GameSekiro::get_scriptedcambuf_checkfun() const {
	return template_check_scriptedcambuf_hash<double, 13, 1>;
}
uint64_t GameSekiro::get_scriptedcambuf_sizebytes() const {
	return template_scriptedcambuf_sizebytes<double, 13, 1>();
}
bool GameSekiro::copy_scriptedcambuf_to_matrix(uint8_t* buf, uint64_t buflen, CamMatrixData& rcam, std::string& errstr) const {
	return template_copy_scriptedcambuf_extrinsic_cam2world_and_fov<double, 13, 1>(buf, buflen, rcam, true, errstr);
}

bool GameSekiro::can_interpret_depth_buffer() const {
	return true;
}
float GameSekiro::convert_to_physical_distance_depth_u64(uint64_t depthval) const {
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

uint64_t GameSekiro::get_scriptedcambuf_triggerbytes() const
{
    // 将 double 类型的注入专用魔数转换为 8 字节的整数
    const double magic_double = 1.20040525131452021e-12;
    uint64_t magic_int;
    static_assert(sizeof(magic_double) == sizeof(magic_int));
    memcpy(&magic_int, &magic_double, sizeof(magic_int));
    return magic_int;
}

void GameSekiro::process_camera_buffer_from_igcs(
    double* camera_data_buffer,
    const float* camera_ue_pos,
    float roll, float pitch, float yaw,
    float fov)
{
    
    // --- 将 main.cpp 中的原始逻辑移动到这里作为默认实现 ---
    float new_pitch = -pitch;
    float new_yaw = -yaw;
    const float cr = cos(roll), sr = sin(roll);
    const float cp = cos(new_pitch), sp = sin(new_pitch);
    const float cy = cos(new_yaw), sy = sin(new_yaw);

    float c2w_col_right[3] = { cy * cp, -sy * cp, sp };
    float c2w_col_up[3] = { cy * sp * sr + sy * cr, -sy * sp * sr + cy * cr, -cp * sr };
    float c2w_col_forward[3] = { -cy * sp * cr + sy * sr, sy * sp * cr + cy * sr, cp * cr };
    
    float camera_target_pos[3] = { camera_ue_pos[1], -camera_ue_pos[2], camera_ue_pos[0] };
    const float pose_scale = 1.3333f; 

    float R_ue_matrix[3][3] = {
        { c2w_col_right[0], c2w_col_up[0], c2w_col_forward[0] },
        { c2w_col_right[1], c2w_col_up[1], c2w_col_forward[1] },
        { c2w_col_right[2], c2w_col_up[2], c2w_col_forward[2] }
    };

    const float M_UE_to_CV[3][3] = { { 0, 1, 0 }, { 0, 0, -1 }, { 1, 0, 0 } };
    const float M_T[3][3] = { { 0, 0, 1 }, { 1, 0, 0 }, { 0, -1, 0 } };

    float temp_matrix[3][3] = { 0.0f };
    for (int i = 0; i < 3; ++i) for (int j = 0; j < 3; ++j) for (int k = 0; k < 3; ++k)
        temp_matrix[i][j] += R_ue_matrix[i][k] * M_T[k][j];

    float R_cv_final[3][3] = { 0.0f };
    for (int i = 0; i < 3; ++i) for (int j = 0; j < 3; ++j) for (int k = 0; k < 3; ++k)
        R_cv_final[i][j] += M_UE_to_CV[i][k] * temp_matrix[k][j];

    float t_cv_final[3] = {
        camera_target_pos[2] * pose_scale,
        camera_target_pos[1] * pose_scale,
        camera_target_pos[0] * pose_scale
    };

    camera_data_buffer[2] = R_cv_final[0][0]; 
    camera_data_buffer[3] = R_cv_final[0][1]; 
    camera_data_buffer[4] = -R_cv_final[0][2]; 
    camera_data_buffer[5] = t_cv_final[0];

    camera_data_buffer[6] = R_cv_final[1][0]; 
    camera_data_buffer[7] = R_cv_final[1][1]; 
    camera_data_buffer[8] = -R_cv_final[1][2]; 
    camera_data_buffer[9] = t_cv_final[2];

    camera_data_buffer[10] = -R_cv_final[2][0]; 
    camera_data_buffer[11] = -R_cv_final[2][1]; 
    camera_data_buffer[12] = R_cv_final[2][2]; 
    camera_data_buffer[13] = t_cv_final[1];

    camera_data_buffer[14] = fov;
    
}