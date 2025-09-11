#pragma once
// Copyright (C) 2022 Jason Bunk
#include "gcv_utils/geometry.h"
#include <nlohmann/json.hpp>
#include <Eigen/Dense>
using Json = nlohmann::json;
struct EulerVec {
    double x = 0.0, y = 0.0, z = 0.0;
};

enum CamMatrixStatus {
	CamMatrix_Uninitialized = 0,
	CamMatrix_PositionGood = 1,
	CamMatrix_RotationGood = 2,
	CamMatrix_AllGood = 3,
	CamMatrix_WIP = 4,
};

struct CamMatrixData {
	CamMatrixStatus extrinsic_status = CamMatrix_Uninitialized;
	CamMatrix extrinsic_cam2world;
	ftype fov_v_degrees = ftype(-9999.0);
	ftype fov_h_degrees = ftype(-9999.0);
	bool intrinsic_status() const { return fov_v_degrees > ftype(0.0) || fov_h_degrees > ftype(0.0); }
	void into_json(nlohmann::json & rj) const;

	bool is_euler_format = false;
    EulerVec pos; 
    EulerVec euler_deg; // x=pitch, y=roll, z=yaw
    float aspect_ratio = 0.f;
	

    void write_json(const std::string& filename) const;
};
