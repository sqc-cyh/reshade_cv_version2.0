#include "msfs_simconnect_manager.h"

#include <cmath>
#include <cstring>
#include <iostream>

double MSFSSimConnectManager::camera_buffer_[17];
std::atomic<double> MSFSSimConnectManager::camera_buffer_counter_{0.0};
constexpr double MAGIC_NUMBER = 1.20040525131452021e-12;

MSFSSimConnectManager& MSFSSimConnectManager::get() {
    static MSFSSimConnectManager instance;
    return instance;
}

MSFSSimConnectManager::MSFSSimConnectManager() {
    std::memset(camera_buffer_, 0, sizeof(camera_buffer_));
    camera_buffer_[0] = MAGIC_NUMBER;
}

MSFSSimConnectManager::~MSFSSimConnectManager() {
    shutdown();
}

bool MSFSSimConnectManager::initialize() {
    if (connected_) return true;

    HRESULT hr = SimConnect_Open(&simconnect_handle_, "MSFS2020_CV_Capture", NULL, 0, 0, 0);
    if (SUCCEEDED(hr)) {
        setup_camera_position_definitions();

        SimConnect_RequestDataOnSimObject(simconnect_handle_, 1, 1, SIMCONNECT_OBJECT_ID_USER, SIMCONNECT_PERIOD_SIM_FRAME);

        connected_ = true;

        return true;
    }
    return false;
}

void MSFSSimConnectManager::update() {
    if (connected_ && simconnect_handle_) {
        SimConnect_CallDispatch(simconnect_handle_, dispatch_proc, this);
    } else
        initialize();
}

void MSFSSimConnectManager::shutdown() {
    if (simconnect_handle_) {
        SimConnect_Close(simconnect_handle_);
        simconnect_handle_ = NULL;
        reshade::log_message(reshade::log_level::info, "MSFS SimConnect: Connection closed");
    }
    connected_ = false;
    has_camera_data_ = false;
}

void MSFSSimConnectManager::setup_camera_position_definitions() {
    struct {
        const char* name;
        const char* unit;
    } vars[] = {
        {"CAMERA GAMEPLAY PITCH YAW:0", "Radians"},
        {"CAMERA GAMEPLAY PITCH YAW:1", "Radians"},
        {"PLANE LATITUDE", "Radians"},
        {"PLANE LONGITUDE", "Radians"},
        {"PLANE ALTITUDE", "Feet"},
        {"PLANE PITCH DEGREES", "Radians"},
        {"PLANE BANK DEGREES", "Radians"},
        {"PLANE HEADING DEGREES TRUE", "Radians"}};

    bool all_success = true;
    for (size_t i = 0; i < std::size(vars); ++i) {
        HRESULT hr = SimConnect_AddToDataDefinition(simconnect_handle_, 1, vars[i].name, vars[i].unit);
        char msg[256];
        if (SUCCEEDED(hr)) {
            snprintf(msg, sizeof(msg), "MSFS SimConnect: Added [%s] successfully", vars[i].name);
            reshade::log_message(reshade::log_level::info, msg);
        } else {
            snprintf(msg, sizeof(msg), "MSFS SimConnect: ❌ FAILED to add [%s], HRESULT=0x%08X", vars[i].name, static_cast<unsigned int>(hr));
            reshade::log_message(reshade::log_level::error, msg);
            all_success = false;
        }
    }

    if (all_success) {
        reshade::log_message(reshade::log_level::info, "MSFS SimConnect: All camera position definitions added successfully");
    } else {
        reshade::log_message(reshade::log_level::warning, "MSFS SimConnect: Some camera definitions failed – this may cause no data callbacks!");
    }
}

void CALLBACK MSFSSimConnectManager::dispatch_proc(SIMCONNECT_RECV* pData, DWORD cbData, void* pContext) {
    auto* manager = static_cast<MSFSSimConnectManager*>(pContext);

    switch (pData->dwID) {
        case SIMCONNECT_RECV_ID_OPEN:
            break;
        case SIMCONNECT_RECV_ID_SIMOBJECT_DATA: {
            auto* pObjData = reinterpret_cast<SIMCONNECT_RECV_SIMOBJECT_DATA*>(pData);
            manager->process_camera_position_data(&pObjData->dwData);
            break;
        }
        case SIMCONNECT_RECV_ID_QUIT: {
            manager->connected_ = false;
            manager->has_camera_data_ = false;
            break;
        }
        default:
            break;
    }
}

void MSFSSimConnectManager::process_camera_position_data(const void* data) {
    const double* d = static_cast<const double*>(data);

    double camera_pitch = d[0];    // 相机俯仰 (弧度)
    double camera_heading = d[1];  // 相机偏航 (弧度)
    double plane_lat = d[2];       // 纬度 (弧度)
    double plane_lon = d[3];       // 经度 (弧度)
    double plane_alt = d[4];       // 高度 (英尺)
    double plane_pitch = d[5];     // 俯仰角 (弧度)
    double plane_roll = d[6];      // 滚转角 (弧度)
    double plane_heading = d[7];   // 航向角 (弧度)

    char raw_log[1024];
    snprintf(raw_log, sizeof(raw_log),
             "Raw data: CamPitch=%.3f, CamHeading=%.3f, Lat=%.3f, Lon=%.3f, Alt=%.3f, PlanePitch=%.3f, PlaneRoll=%.3f, PlaneHeading=%.3f",
             camera_pitch, camera_heading,
             plane_lat, plane_lon, plane_alt,
             plane_pitch, plane_roll, plane_heading);
    reshade::log_message(reshade::log_level::info, raw_log);

    bool valid = true;
    for (int i = 0; i < 8; ++i) {
        if (!std::isfinite(d[i])) {
            valid = false;
            break;
        }
    }

    if (valid) {
        convert_from_position_and_rotation(
            camera_pitch, camera_heading,
            plane_lat, plane_lon, plane_alt,
            plane_pitch, plane_roll, plane_heading);
        has_position_data_ = true;
        has_camera_data_ = true;
    } else {
        reshade::log_message(reshade::log_level::warning, "MSFS SimConnect: Invalid camera position data received");
    }
}

static void mat3_mul(double R_out[9], const double R_a[9], const double R_b[9]) {
    R_out[0] = R_a[0] * R_b[0] + R_a[1] * R_b[3] + R_a[2] * R_b[6];
    R_out[1] = R_a[0] * R_b[1] + R_a[1] * R_b[4] + R_a[2] * R_b[7];
    R_out[2] = R_a[0] * R_b[2] + R_a[1] * R_b[5] + R_a[2] * R_b[8];

    R_out[3] = R_a[3] * R_b[0] + R_a[4] * R_b[3] + R_a[5] * R_b[6];
    R_out[4] = R_a[3] * R_b[1] + R_a[4] * R_b[4] + R_a[5] * R_b[7];
    R_out[5] = R_a[3] * R_b[2] + R_a[4] * R_b[5] + R_a[5] * R_b[8];

    R_out[6] = R_a[6] * R_b[0] + R_a[7] * R_b[3] + R_a[8] * R_b[6];
    R_out[7] = R_a[6] * R_b[1] + R_a[7] * R_b[4] + R_a[8] * R_b[7];
    R_out[8] = R_a[6] * R_b[2] + R_a[7] * R_b[5] + R_a[8] * R_b[8];
}

static void build_aircraft_rotation_matrix(double R[9], double yaw, double pitch, double roll) {
    double cy = cos(yaw), sy = sin(yaw);
    double cp = cos(pitch), sp = sin(pitch);
    double cr = cos(roll), sr = sin(roll);

    R[0] = cy * cp;
    R[1] = cy * sp * sr - sy * cr;
    R[2] = cy * sp * cr + sy * sr;

    R[3] = sy * cp;
    R[4] = sy * sp * sr + cy * cr;
    R[5] = sy * sp * cr - cy * sr;

    R[6] = -sp;
    R[7] = cp * sr;
    R[8] = cp * cr;
}

void MSFSSimConnectManager::convert_from_position_and_rotation(
    double cam_rel_pitch, double cam_rel_heading,
    double plane_lat, double plane_lon, double plane_alt,
    double plane_pitch, double plane_roll, double plane_heading) {
    const double feet_to_meters = 0.3048;
    double altitude_meters = plane_alt * feet_to_meters;

    const double earth_radius = 6378137.0;
    double pos_x = plane_lon * earth_radius * cos(plane_lat);  // East
    double pos_y = plane_lat * earth_radius;                   // North
    double pos_z = -altitude_meters;                           // Down (Z-down)

    // Step 1: Build aircraft rotation (world -> aircraft body)
    double R_aircraft[9];
    build_aircraft_rotation_matrix(R_aircraft, plane_heading, plane_pitch, plane_roll);

    // Step 2: Build camera relative rotation (aircraft body -> camera)
    // In aircraft body: X=forward, Y=right, Z=down
    // Camera relative yaw: around Z (down)
    // Camera relative pitch: around Y (right)
    double ch = cos(cam_rel_heading), sh = sin(cam_rel_heading);
    double cp = cos(cam_rel_pitch), sp = sin(cam_rel_pitch);

    // R_cam_rel = R_z(cam_rel_yaw) * R_y(cam_rel_pitch)
    double R_cam_rel[9] = {
        ch * cp, -sh, ch * sp,
        sh * cp, ch, sh * sp,
        -sp, 0.0, cp};

    // Step 3: Total rotation: world -> camera = R_aircraft * R_cam_rel
    double R_total[9];
    mat3_mul(R_total, R_aircraft, R_cam_rel);

    // Now R_total is the rotation from world to camera.
    // But we want camera-to-world for pose (i.e., where camera is in world).
    // So take transpose (since rotation matrix is orthogonal).
    double R_cam_to_world[9] = {
        R_total[0], R_total[3], R_total[6],
        R_total[1], R_total[4], R_total[7],
        R_total[2], R_total[5], R_total[8]};

    camera_buffer_[0] = MAGIC_NUMBER;

    camera_buffer_[2] = R_cam_to_world[0];  // r00
    camera_buffer_[3] = R_cam_to_world[1];  // r01
    camera_buffer_[4] = R_cam_to_world[2];  // r02

    camera_buffer_[6] = R_cam_to_world[3];  // r10
    camera_buffer_[7] = R_cam_to_world[4];  // r11
    camera_buffer_[8] = R_cam_to_world[5];  // r12

    camera_buffer_[10] = R_cam_to_world[6];  // r20
    camera_buffer_[11] = R_cam_to_world[7];  // r21
    camera_buffer_[12] = R_cam_to_world[8];  // r22

    // Position
    camera_buffer_[5] = pos_x;   // x
    camera_buffer_[9] = pos_y;   // y
    camera_buffer_[13] = pos_z;  // z

    camera_buffer_[14] = 60.0;  // FOV or unused

    double new_counter = camera_buffer_counter_.load() + 1.0;
    if (new_counter > 9999.5) new_counter = 1.0;
    camera_buffer_counter_.store(new_counter);
    camera_buffer_[1] = new_counter;

    update_buffer_hashes();

    char buf_log[512];
    snprintf(buf_log, sizeof(buf_log),
             "Camera pose: counter=%.0f, pos=(%.1f,%.1f,%.1f)",
             camera_buffer_[1], camera_buffer_[5], camera_buffer_[9], camera_buffer_[13]);
    reshade::log_message(reshade::log_level::info, buf_log);
}

void MSFSSimConnectManager::update_buffer_hashes() {
    double poshash1 = 0.0;
    double poshash2 = 0.0;

    for (int i = 1; i <= 14; ++i) {
        poshash1 += camera_buffer_[i];
        poshash2 += (i % 2 == 0 ? -camera_buffer_[i] : camera_buffer_[i]);
    }

    camera_buffer_[15] = poshash1;
    camera_buffer_[16] = poshash2;
}