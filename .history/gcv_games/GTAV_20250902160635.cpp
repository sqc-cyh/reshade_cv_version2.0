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
    // 将 u64 (实际上是 u32) 的位模式重新解释为 float
    uint32_t depth_as_u32 = static_cast<uint32_t>(depthval);
    float depth;
    std::memcpy(&depth, &depth_as_u32, sizeof(float));

    // 如果深度值非常小，可能代表天空或非常远，直接返回大值
    // 这个阈值可以根据需要调整，0.0001f 是一个不错的起点
    // if (depth < 0.0001f) {
    //     return 10000.0f;
    // }

    // 根据曲线拟合得出的公式 (稳健模型)
    return 0.11561731f / (depth + 0.00007021f);
}
