#pragma once
// Copyright (C) 2022 Jason Bunk
#include "gcv_utils/simple_packed_buf.h"
#include "game_interface_factory_registration.h"
#include "gcv_utils/camera_data_struct.h"
#include <Windows.h>
#include <string>
namespace reshade { namespace api { struct effect_runtime; } }
class GameInterface {
protected:
	HANDLE mygame_handle_exe = 0;
	bool init_get_game_exe();
public:
	virtual ~GameInterface() {}

	virtual std::string gamename_simpler() const = 0;
	virtual std::string gamename_verbose() const = 0;

	// can do on game startup
	virtual bool init_on_startup() { return init_get_game_exe(); }

	// wait until in 3D rendered world, not main menu
	virtual bool init_in_game() = 0;

	// return a camera matrix status from above enum (can return multiple flags)
	virtual bool get_camera_matrix(CamMatrixData &rcam, std::string &errstr) = 0;

	// convert from integer depth to floating-point distance
	virtual bool can_interpret_depth_buffer() const { return false; }
	virtual float convert_to_physical_distance_depth_u64(uint64_t depthval) const { return 0.0f; }

	// memory scans
	virtual bool scan_all_memory_for_scripted_cam_matrix(std::string& errstr) { errstr += "not implemented"; return false; }
	virtual bool update_camera_buffer_from_reshade(reshade::api::effect_runtime* /*runtime*/, double* /*out*/, size_t /*out_len*/, double& /*counter*/) const{return false;}
	// 新增的虚函数，用于处理来自IGCS的相机数据
    virtual void process_camera_buffer_from_igcs(double* camera_data_buffer, const float* camera_ue_pos, float roll, float pitch, float yaw, float fov){
        // 默认实现：不进行任何处理，仅清空相机位姿数据。
		for (int i = 2; i <= 14; ++i){
			camera_data_buffer[i] = 0.0;
		}
    }

};
