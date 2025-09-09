#pragma once
// Copyright (C)
// GTA5 (Enhanced) adapter

#include "game_with_camera_data_in_one_dll.h"

class GameGTAV : public GameWithCameraDataInOneDLL {
protected:
    // camera data comes from scripted buffer in the main exe memory
    std::string camera_dll_name() const override { return ""; }
    uint64_t    camera_dll_mem_start() const override { return 0; }
    GameCamDLLMatrixType camera_dll_matrix_format() const override {
        return GameCamDLLMatrix_allmemscanrequiredtofindscriptedcambuf;
    }

public:
    std::string gamename_simpler() const override { return "GTAV"; }
    std::string gamename_verbose() const override { return "Grand Theft Auto V Enhanced"; }

    // scripted buffer: we use double[13] layout (cam->world extrinsic + FOV),
    // with the same float-hash check as other titles using scripted buffers.
    scriptedcam_checkbuf_funptr get_scriptedcambuf_checkfun() const override;
    uint64_t get_scriptedcambuf_sizebytes() const override;
    bool copy_scriptedcambuf_to_matrix(uint8_t* buf, uint64_t buflen,
                                       CamMatrixData& rcam, std::string& errstr) const override;

    // depth buffer → physical distance (meters)
    bool  can_interpret_depth_buffer() const override;
    float convert_to_physical_distance_depth_u64(uint64_t depthval) const override;
};

// Register for both classic and enhanced executable names
REGISTER_GAME_INTERFACE(GameGTAV, 0, "gta5.exe");
REGISTER_GAME_INTERFACE(GameGTAV, 1, "gta5_enhanced.exe");
