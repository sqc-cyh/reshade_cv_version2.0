// Copyright (C) 2022 Jason Bunk
#include "DevilMayCry5.h"
#include "gcv_utils/depth_utils.h"
#include "gcv_utils/scripted_cam_buf_templates.h"


std::string GameDevilMayCry5::gamename_verbose() const { return "DevilMayCry5"; } // hopefully continues to work with future patches via the mod lua

std::string GameDevilMayCry5::camera_dll_name() const { return ""; } // no dll name, it's available in the exe memory space
uint64_t GameDevilMayCry5::camera_dll_mem_start() const { return 0; }
GameCamDLLMatrixType GameDevilMayCry5::camera_dll_matrix_format() const { return GameCamDLLMatrix_allmemscanrequiredtofindscriptedcambuf; }

scriptedcam_checkbuf_funptr GameDevilMayCry5::get_scriptedcambuf_checkfun() const {
	return template_check_scriptedcambuf_hash<double, 13, 1>;
}
uint64_t GameDevilMayCry5::get_scriptedcambuf_sizebytes() const {
	return template_scriptedcambuf_sizebytes<double, 13, 1>();
}
bool GameDevilMayCry5::copy_scriptedcambuf_to_matrix(uint8_t* buf, uint64_t buflen, CamMatrixData& rcam, std::string& errstr) const {
	return template_copy_scriptedcambuf_extrinsic_cam2world_and_fov<double, 13, 1>(buf, buflen, rcam, false, errstr);
}

bool GameDevilMayCry5::can_interpret_depth_buffer() const {
	return true;
}
float GameDevilMayCry5::convert_to_physical_distance_depth_u64(uint64_t depthval) const {
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

uint64_t GameDevilMayCry5::get_scriptedcambuf_triggerbytes() const
{
    // 将 double 类型的注入专用魔数转换为 8 字节的整数
    const double magic_double = 1.20040525131452021e-12;
    uint64_t magic_int;
    static_assert(sizeof(magic_double) == sizeof(magic_int));
    memcpy(&magic_int, &magic_double, sizeof(magic_int));
    return magic_int;
}

void GameDevilMayCry5::process_camera_buffer_from_igcs(
    double* camera_data_buffer,
    const float* camera_ue_pos, // 对应 Python 中的 location {x, y, z}
    float roll, float pitch, float yaw, // 弧度
    float fov)
{
    // --- 严格按照 Python 脚本逻辑重写 ---

    // 步骤 1: 计算 UE 坐标系下的旋转矩阵 R_ue (C2W)
    // Python 中使用了 -yaw，这里也对 yaw 取反
    const float new_pitch = -pitch;
	const float new_roll = -roll;


    // 根据 Python 中的 rot_x_lh, rot_y_lh, rot_z_lh 定义
    const float cr = cos(new_roll), sr = sin(new_roll);
    const float cp = cos(new_pitch), sp = sin(new_pitch);
    const float cz = cos(yaw), sz = sin(yaw);

    // R_x(roll)
    const float Rx[3][3] = {
        { 1,  0,   0  },
        { 0,  cr,  sr },
        { 0, -sr,  cr }
    };
    // R_y(pitch)
    const float Ry[3][3] = {
        { cp,  0, -sp },
        { 0,   1,  0  },
        { sp,  0,  cp }
    };
    // R_z(-yaw)
    const float Rz[3][3] = {
        { cz,  sz, 0 },
        { -sz, cz, 0 },
        { 0,   0,  1 }
    };

    // R_ue = Rz @ Ry @ Rx
    float R_ue_temp[3][3] = {0};
    float R_ue[3][3] = {0};
    // R_ue_temp = Rz @ Ry
    for (int i = 0; i < 3; ++i) for (int j = 0; j < 3; ++j) for (int k = 0; k < 3; ++k)
        R_ue_temp[i][j] += Rz[i][k] * Ry[k][j];
    // R_ue = R_ue_temp @ Rx
    for (int i = 0; i < 3; ++i) for (int j = 0; j < 3; ++j) for (int k = 0; k < 3; ++k)
        R_ue[i][j] += R_ue_temp[i][k] * Rx[k][j];

    // 步骤 2: 将 R_ue 转换到 OpenCV 坐标系
    // R_cv = M_UE_to_CV @ R_ue @ M_UE_to_CV.T
    const float M_UE_to_CV[3][3] = {
        { 0, 1,  0 },
        { 0, 0, -1 },
        { 1, 0,  0 }
    };
    const float M_UE_to_CV_T[3][3] = {
        { 0, 0, 1 },
        { 1, 0, 0 },
        { 0,-1, 0 }
    };

    float R_cv_temp[3][3] = {0};
    float R_cv[3][3] = {0};
    // R_cv_temp = R_ue @ M_UE_to_CV.T
    for (int i = 0; i < 3; ++i) for (int j = 0; j < 3; ++j) for (int k = 0; k < 3; ++k)
        R_cv_temp[i][j] += R_ue[i][k] * M_UE_to_CV_T[k][j];
    // R_cv = M_UE_to_CV @ R_cv_temp
    for (int i = 0; i < 3; ++i) for (int j = 0; j < 3; ++j) for (int k = 0; k < 3; ++k)
        R_cv[i][j] += M_UE_to_CV[i][k] * R_cv_temp[k][j];

    // 步骤 3: 转换并缩放平移向量 t_cv
    // t_cv = [location.z, location.y, location.x] / 100.0
    // camera_ue_pos[0] = x, [1] = y, [2] = z
    const float scale = 0.5f;
    const float t_cv[3] = {
        camera_ue_pos[0] * scale,  // t_cv[0] = x
        camera_ue_pos[1] * scale, // t_cv[1] = y
        camera_ue_pos[2] * scale, // t_cv[2] = z
    };

    // 步骤 4: 将最终的 c2w (R_cv, t_cv) 矩阵填充到缓冲区
    // 第一行
    camera_data_buffer[2] = R_cv[0][0];
    camera_data_buffer[3] = R_cv[0][1];
    camera_data_buffer[4] = R_cv[0][2];
    camera_data_buffer[5] = t_cv[0];
    // 第二行
    camera_data_buffer[6] = R_cv[1][0];
    camera_data_buffer[7] = R_cv[1][1];
    camera_data_buffer[8] = R_cv[1][2];
    camera_data_buffer[9] = t_cv[1];
    // 第三行
    camera_data_buffer[10] = R_cv[2][0];
    camera_data_buffer[11] = R_cv[2][1];
    camera_data_buffer[12] = R_cv[2][2];
    camera_data_buffer[13] = t_cv[2];
    // FOV
    camera_data_buffer[14] = fov;
}


