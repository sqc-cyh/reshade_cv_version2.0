// Copyright (C) 2022 Jason Bunk
#include "Cyberpunk2077.h"
#include "gcv_utils/depth_utils.h"
#include "gcv_utils/scripted_cam_buf_templates.h"

// For this game, scripting is made easy by Cyber Engine Tweaks,
// so I provide a simple lua script which stashes camera coordinates into a double[] buffer.

std::string GameCyberpunk2077::gamename_verbose() const { return "Cyberpunk2077"; } // hopefully continues to work with future patches via the mod lua

std::string GameCyberpunk2077::camera_dll_name() const { return ""; } // no dll name, it's available in the exe memory space
uint64_t GameCyberpunk2077::camera_dll_mem_start() const { return 0; }
GameCamDLLMatrixType GameCyberpunk2077::camera_dll_matrix_format() const { return GameCamDLLMatrix_allmemscanrequiredtofindscriptedcambuf; }

scriptedcam_checkbuf_funptr GameCyberpunk2077::get_scriptedcambuf_checkfun() const {
	return template_check_scriptedcambuf_hash<double, 13, 1>;
}
uint64_t GameCyberpunk2077::get_scriptedcambuf_sizebytes() const {
	return template_scriptedcambuf_sizebytes<double, 13, 1>();
}
bool GameCyberpunk2077::copy_scriptedcambuf_to_matrix(uint8_t* buf, uint64_t buflen, CamMatrixData& rcam, std::string& errstr) const {
	return template_copy_scriptedcambuf_extrinsic_cam2world_and_fov<double, 13, 1>(buf, buflen, rcam, true, errstr);
}

bool GameCyberpunk2077::can_interpret_depth_buffer() const {
	return true;
}
float GameCyberpunk2077::convert_to_physical_distance_depth_u64(uint64_t depthval) const {
    // 这些常量需要与你在 ReShade 中的全局预处理器定义设置匹配。
    // 假设你的全局预处理器定义设置为 1, 1 和 1000.0
    // RESHADE_DEPTH_INPUT_IS_LOGARITHMIC = 1
    // RESHADE_DEPTH_INPUT_IS_REVERSED = 1
    // RESHADE_DEPTH_LINEARIZATION_FAR_PLANE = 1000.0

    const bool IS_LOGARITHMIC = true;
    const bool IS_REVERSED = true;
    const float FAR_PLANE = 1000.0f;
    const float NEAR_PLANE = 1.0f;
    
    // 第1步：将原始的 32 位深度值归一化为 0.0 到 1.0 之间的浮点数
    // 我们只使用 64 位值的前 32 位，因为深度缓冲区是 32 位的。
    float normalized_depth = static_cast<float>(depthval & 0xFFFFFFFF) / 4294967295.0f;

    // 第2步：如果需要，应用对数到线性的转换
    if (IS_LOGARITHMIC) {
        const float C = 0.01f;
        normalized_depth = (exp(normalized_depth * log(C + 1.0f)) - 1.0f) / C;
    }

    // 第3步：如果需要，反转深度值
    if (IS_REVERSED) {
        normalized_depth = 1.0f - normalized_depth;
    }
    
    // 第4步：根据远平面距离进行最终的线性化
    float linearized_depth = normalized_depth / (FAR_PLANE - normalized_depth * (FAR_PLANE - NEAR_PLANE));

    return linearized_depth;
}