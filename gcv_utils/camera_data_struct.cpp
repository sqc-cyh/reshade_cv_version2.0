#include "camera_data_struct.h"
#include <vector>
#include <string>

// 使用与 .h 文件一致的成员变量名
void CamMatrixData::into_json(nlohmann::json & rj) const {
    if (is_euler_format) {
        rj["fov"] = fov_v_degrees; // 使用 fov_v_degrees
        rj["aspect_ratio"] = aspect_ratio;
        rj["location"] = {
            {"x", pos.x}, 
            {"y", pos.y}, 
            {"z", pos.z}
        };
        rj["rotation"] = {
            {"pitch", euler_deg.x}, 
            {"roll", euler_deg.y}, 
            {"yaw", euler_deg.z}
        };
    } else {
        std::vector<double> flat_matrix;
        flat_matrix.reserve(12);
        for (int r = 0; r < 3; ++r) {
            for (int c = 0; c < 4; ++c) {
                // 使用 extrinsic_cam2world
                flat_matrix.push_back(extrinsic_cam2world(r, c));
            }
        }
        rj["extrinsic_cam2world"] = flat_matrix;
        rj["fov_v_degrees"] = fov_v_degrees; // 使用 fov_v_degrees
    }
}