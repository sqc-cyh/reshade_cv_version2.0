#pragma once
// Copyright (C)
// GTA5 (Enhanced) adapter (CP2077-style layout)

#include "game_with_camera_data_in_one_dll.h"

class GameGTAV : public GameWithCameraDataInOneDLL {
protected:
    // For GTA5 Enhanced we also scan the main exe memory for the scripted camera buffer
    virtual std::string camera_dll_name() const override { return ""; }
    virtual uint64_t    camera_dll_mem_start() const override { return 0; }
    virtual GameCamDLLMatrixType camera_dll_matrix_format() const override {
        return GameCamDLLMatrix_allmemscanrequiredtofindscriptedcambuf;
    }

public:
    virtual std::string gamename_simpler() const override { return "GTAV"; }
    virtual std::string gamename_verbose() const override { return "Grand Theft Auto V Enhanced"; }

    // scripted buffer: double[13] => cam->world (3x4) + 1 slot for FOV, HASH=1
    virtual scriptedcam_checkbuf_funptr get_scriptedcambuf_checkfun() const override;
    virtual uint64_t get_scriptedcambuf_sizebytes() const override;
    virtual bool copy_scriptedcambuf_to_matrix(uint8_t* buf, uint64_t buflen,
                                               CamMatrixData& rcam, std::string& errstr) const override;

    // depth buffer → physical distance (meters)
    virtual bool  can_interpret_depth_buffer() const override;
    virtual float convert_to_physical_distance_depth_u64(uint64_t depthval) const override;
};

// Register both classic/enhanced names if needed
REGISTER_GAME_INTERFACE(GameGTAV, 0, "gta5.exe");
REGISTER_GAME_INTERFACE(GameGTAV, 1, "gta5_enhanced.exe");
