// Copyright (C) 2022 Jason Bunk
#include "Witcher3.h"
#include "gcv_utils/depth_utils.h"
#include "gcv_utils/scripted_cam_buf_templates.h"


std::string GameWitcher3::gamename_verbose() const { return "Witcher3"; } // hopefully continues to work with future patches via the mod lua

std::string GameWitcher3::camera_dll_name() const { return ""; } // no dll name, it's available in the exe memory space
uint64_t GameWitcher3::camera_dll_mem_start() const { return 0; }
GameCamDLLMatrixType GameWitcher3::camera_dll_matrix_format() const { return GameCamDLLMatrix_allmemscanrequiredtofindscriptedcambuf; }

scriptedcam_checkbuf_funptr GameWitcher3::get_scriptedcambuf_checkfun() const {
	return template_check_scriptedcambuf_hash<double, 13, 1>;
}
uint64_t GameWitcher3::get_scriptedcambuf_sizebytes() const {
	return template_scriptedcambuf_sizebytes<double, 13, 1>();
}
bool GameWitcher3::copy_scriptedcambuf_to_matrix(uint8_t* buf, uint64_t buflen, CamMatrixData& rcam, std::string& errstr) const {
	return template_copy_scriptedcambuf_extrinsic_cam2world_and_fov<double, 13, 1>(buf, buflen, rcam, true, errstr);
}

bool GameWitcher3::can_interpret_depth_buffer() const {
	return true;
}
float GameWitcher3::convert_to_physical_distance_depth_u64(uint64_t depthval) const {
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

uint64_t GameWitcher3::get_scriptedcambuf_triggerbytes() const
{
    // 将 double 类型的注入专用魔数转换为 8 字节的整数
    const double magic_double = 1.20040525131452021e-12;
    uint64_t magic_int;
    static_assert(sizeof(magic_double) == sizeof(magic_int));
    memcpy(&magic_int, &magic_double, sizeof(magic_int));
    return magic_int;
}

void GameWitcher3::process_camera_buffer_from_igcs(
    double* camera_data_buffer,
    const float* camera_ue_pos, // 对应 Python 中的 location {x, y, z}
    float roll, float pitch, float yaw, // 弧度（对应 Python 的 rotation['roll'], ['pitch'], ['yaw']）
    float fov)
{
    // --- 严格对齐 Python 脚本逻辑：关键修改点已标注 ---

    // 步骤 1: 计算 UE 坐标系下的旋转矩阵 R_ue (C2W)
    // 核心变化1：Python 中 build_cv_c2w_from_ue 调用 ue_rotator_to_R_world 时，参数顺序为 (yaw, roll, pitch)
    // Python ue_rotator_to_R_world(roll_deg, pitch_deg, yaw_deg) = 传入 (yaw, roll, pitch)
    // 故 C++ 中旋转矩阵参数对应：Rx(roll_deg=yaw)、Ry(pitch_deg=roll)、Rz(yaw_deg=pitch)，且无 yaw 取负
    const float cr = cos(yaw);    // Rx 对应 Python 的 roll_deg = rotation['yaw']（C++ 的 yaw 参数）
    const float sr = sin(yaw);
    const float cp = cos(roll);   // Ry 对应 Python 的 pitch_deg = rotation['roll']（C++ 的 roll 参数）
    const float sp = sin(roll);
    const float cz = cos(pitch);  // Rz 对应 Python 的 yaw_deg = rotation['pitch']（C++ 的 pitch 参数）
    const float sz = sin(pitch);

    // 旋转矩阵定义（完全匹配 Python 的 rot_x_lh/rot_y_lh/rot_z_lh）
    // R_x(roll_deg=yaw)
    const float Rx[3][3] = {
        { 1,  0,   0  },
        { 0,  cr,  sr },
        { 0, -sr,  cr }
    };
    // R_y(pitch_deg=roll)
    const float Ry[3][3] = {
        { cp,  0, -sp },
        { 0,   1,  0  },
        { sp,  0,  cp }
    };
    // R_z(yaw_deg=pitch)
    const float Rz[3][3] = {
        { cz,  sz, 0 },
        { -sz, cz, 0 },
        { 0,   0,  1 }
    };

    // R_ue = Rz @ Ry @ Rx（Python 乘法顺序，显式清零避免垃圾值）
    float R_ue_temp[3][3] = {0}; // 临时存储 Rz @ Ry
    float R_ue[3][3] = {0};      // 最终 R_ue = (Rz @ Ry) @ Rx
    // 计算 Rz @ Ry
    for (int i = 0; i < 3; ++i) {
        for (int j = 0; j < 3; ++j) {
            R_ue_temp[i][j] = 0;
            for (int k = 0; k < 3; ++k) {
                R_ue_temp[i][j] += Rz[i][k] * Ry[k][j];
            }
        }
    }
    // 计算 (Rz @ Ry) @ Rx
    for (int i = 0; i < 3; ++i) {
        for (int j = 0; j < 3; ++j) {
            R_ue[i][j] = 0;
            for (int k = 0; k < 3; ++k) {
                R_ue[i][j] += R_ue_temp[i][k] * Rx[k][j];
            }
        }
    }

    // 步骤 2: 将 R_ue 转换到 OpenCV 坐标系（Python 逻辑不变：R_cv = M_UE_to_CV @ R_ue @ M_UE_to_CV.T）
    const float M_UE_to_CV[3][3] = {
        { 0, 1,  0 },
        { 0, 0, -1 },
        { 1, 0,  0 }
    };
    const float M_UE_to_CV_T[3][3] = { // M_UE_to_CV 的转置（提前计算，避免运行时转置）
        { 0, 0, 1 },
        { 1, 0, 0 },
        { 0,-1, 0 }
    };

    float R_cv_temp[3][3] = {0}; // 临时存储 R_ue @ M_UE_to_CV.T
    float R_cv[3][3] = {0};      // 最终 R_cv
    // 计算 R_ue @ M_UE_to_CV.T
    for (int i = 0; i < 3; ++i) {
        for (int j = 0; j < 3; ++j) {
            R_cv_temp[i][j] = 0;
            for (int k = 0; k < 3; ++k) {
                R_cv_temp[i][j] += R_ue[i][k] * M_UE_to_CV_T[k][j];
            }
        }
    }
    // 计算 M_UE_to_CV @ (R_ue @ M_UE_to_CV.T)
    for (int i = 0; i < 3; ++i) {
        for (int j = 0; j < 3; ++j) {
            R_cv[i][j] = 0;
            for (int k = 0; k < 3; ++k) {
                R_cv[i][j] += M_UE_to_CV[i][k] * R_cv_temp[k][j];
            }
        }
    }

    // 步骤 3: 转换并缩放平移向量 t_cv（核心变化2：匹配 Python 的平移逻辑）
    // Python 逻辑：t_cv = [location.z, location.y, location.x] * pose_scale(0.5)
    // camera_ue_pos 对应 Python location：[0]=x, [1]=y, [2]=z
    const float pose_scale = 0.5f; // 替换原 0.01f，对齐 Python 的 pose_scale=0.5
    const float t_cv[3] = {
        camera_ue_pos[0] * pose_scale, // t_cv[0] = UE location.z * 0.5
        -camera_ue_pos[2] * pose_scale, // t_cv[1] = UE location.y * 0.5
        camera_ue_pos[1] * pose_scale  // t_cv[2] = UE location.x * 0.5
    };

    // 步骤 4: 填充 c2w 矩阵到缓冲区（核心变化3：移除错误的 R_cv 符号反转）
    // Python 中 c2w 直接使用 R_cv 和 t_cv，无符号修改
    // 第一行：[R_cv[0][0], R_cv[0][1], R_cv[0][2], t_cv[0]]
    camera_data_buffer[2] = R_cv[0][0];
    camera_data_buffer[3] = -R_cv[0][1];
    camera_data_buffer[4] = -R_cv[0][2];
    camera_data_buffer[5] = t_cv[0];
    // 第二行：[R_cv[1][0], R_cv[1][1], R_cv[1][2], t_cv[1]]
    camera_data_buffer[6] = R_cv[1][0];
    camera_data_buffer[7] = -R_cv[1][1];
    camera_data_buffer[8] = -R_cv[1][2];
    camera_data_buffer[9] = t_cv[1];
    // 第三行：[R_cv[2][0], R_cv[2][1], R_cv[2][2], t_cv[2]]
    camera_data_buffer[10] = R_cv[2][0];
    camera_data_buffer[11] = -R_cv[2][1];
    camera_data_buffer[12] = -R_cv[2][2];
    camera_data_buffer[13] = t_cv[2];
    // FOV（保持与 Python 一致，传递原始 fov 值）
    camera_data_buffer[14] = fov;
}