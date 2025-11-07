#include "msfs_simconnect_manager.h"

#include <cmath>
#include <cstring>
#include <iostream>

double MSFSSimConnectManager::camera_buffer_[17];
std::atomic<double> MSFSSimConnectManager::camera_buffer_counter_{0.0};
constexpr float MAGIC_NUMBER = 1.20040525131452021e-12f;

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

    double camera_pitch = d[0];   // 相机俯仰 (弧度)
    double camera_yaw = d[1];     // 相机偏航 (弧度)
    double plane_lat = d[2];      // 纬度 (弧度)
    double plane_lon = d[3];      // 经度 (弧度)
    double plane_alt = d[4];      // 高度 (英尺)
    double plane_pitch = d[5];    // 俯仰角 (弧度)
    double plane_bank = d[6];     // 滚转角 (弧度)
    double plane_heading = d[7];  // 航向角 (弧度)

    char raw_log[512];
    snprintf(raw_log, sizeof(raw_log),
             "Raw data: CamPitch=%.3f, CamYaw=%.3f, Lat=%.6f, Lon=%.6f, Alt=%.1f, PlanePitch=%.3f, PlaneBank=%.3f, PlaneHeading=%.3f",
             camera_pitch, camera_yaw, plane_lat, plane_lon, plane_alt,
             plane_pitch, plane_bank, plane_heading);
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
            plane_lat, plane_lon, plane_alt,
            plane_pitch, plane_bank, plane_heading,
            camera_pitch, camera_yaw);
        has_position_data_ = true;
        has_camera_data_ = true;
    } else {
        reshade::log_message(reshade::log_level::warning, "MSFS SimConnect: Invalid camera position data received");
    }
}

void MSFSSimConnectManager::convert_from_position_and_rotation(
    double camera_pitch, double camera_yaw,
    double plane_lat, double plane_lon, double plane_alt,
    double plane_pitch, double plane_bank, double plane_heading) {
    const double feet_to_meters = 0.3048;
    double altitude_meters = plane_alt * feet_to_meters;

    const double earth_radius = 6378137.0;
    double pos_x = plane_lon * earth_radius * cos(plane_lat);
    double pos_y = plane_lat * earth_radius;
    double pos_z = -altitude_meters;

    double pitch = camera_pitch;
    double bank = 0.0;
    double heading = camera_yaw;

    camera_buffer_[0] = MAGIC_NUMBER;

    double ch = cos(heading), sh = sin(heading);
    double cp = cos(pitch), sp = sin(pitch);
    double cb = cos(bank), sb = sin(bank);

    double r11 = ch * cb + sh * sp * sb;
    double r12 = -ch * sb + sh * sp * cb;
    double r13 = sh * cp;

    double r21 = sb * cp;
    double r22 = cb * cp;
    double r23 = -sp;

    double r31 = -sh * cb + ch * sp * sb;
    double r32 = sb * sh + ch * sp * cb;
    double r33 = ch * cp;

    camera_buffer_[2] = r11;
    camera_buffer_[3] = r12;
    camera_buffer_[4] = -r13;
    camera_buffer_[5] = pos_x;

    camera_buffer_[6] = r21;
    camera_buffer_[7] = r22;
    camera_buffer_[8] = -r23;
    camera_buffer_[9] = pos_y;

    camera_buffer_[10] = -r31;
    camera_buffer_[11] = -r32;
    camera_buffer_[12] = r33;
    camera_buffer_[13] = pos_z;

    camera_buffer_[14] = 60.0;

    double new_counter = camera_buffer_counter_.load() + 1.0;
    if (new_counter > 9999.5) new_counter = 1.0;
    camera_buffer_counter_.store(new_counter);
    camera_buffer_[1] = new_counter;

    update_buffer_hashes();

    char buf_log[512];
    snprintf(buf_log, sizeof(buf_log),
             "Camera pose: counter=%.0f, pos=(%.1f,%.1f,%.1f), angles=(pitch=%.3f,heading=%.3f)",
             camera_buffer_[1], camera_buffer_[5], camera_buffer_[9], camera_buffer_[13],
             pitch, heading);
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