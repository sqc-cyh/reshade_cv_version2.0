// Copyright (C)
// GTA5 (Enhanced) adapter header, same layout style as Cyberpunk2077.h

#pragma once

#include "game_with_camera_data_in_one_dll.h"

class GameGTAV : public GameWithCameraDataInOneDLL {
public:
    virtual std::string gamename_simpler() const override { return "GTAV"; }
    virtual std::string gamename_verbose() const override; 

    // DLL/内存相关（GTA5 直接在 exe 内存扫描，不固定 DLL）
    virtual std::string camera_dll_name() const override;
    virtual uint64_t camera_dll_mem_start() const override;
    virtual GameCamDLLMatrixType camera_dll_matrix_format() const override;

    virtual scriptedcam_checkbuf_funptr get_scriptedcambuf_checkfun() const override;
    virtual uint64_t get_scriptedcambuf_sizebytes() const override;
    virtual bool copy_scriptedcambuf_to_matrix(uint8_t* buf, uint64_t buflen,
                                               CamMatrixData& rcam, std::string& errstr) const override;

    // 深度解释
    virtual bool  can_interpret_depth_buffer() const override;
    virtual float convert_to_physical_distance_depth_u64(uint64_t depthval) const override;
};

REGISTER_GAME_INTERFACE(GameGTAV, 0, "gta5.exe");
REGISTER_GAME_INTERFACE(GameGTAV, 1, "gta5_enhanced.exe");
