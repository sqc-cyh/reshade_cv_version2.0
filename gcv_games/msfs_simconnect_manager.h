#pragma once

#include <Windows.h>
#include <SimConnect.h>

#include <array>
#include <atomic>
#include <cstdint>
#include <reshade.hpp>
class MSFSSimConnectManager {
   public:
    static MSFSSimConnectManager& get();

    bool initialize();
    void update();
    void shutdown();

    bool is_connected() const { return connected_; }
    bool has_camera_data() const { return has_camera_data_; }

    static double* get_camera_buffer() { return camera_buffer_; }

   private:
    MSFSSimConnectManager();
    ~MSFSSimConnectManager();

    int connect_attempt_ = 0;
    bool connection_failed_permanently_ = false;

    void setup_camera_position_definitions();

    void process_camera_position_data(const void* data);

    void convert_from_position_and_rotation(
        double camera_pitch, double camera_heading,
        double plane_lat, double plane_lon, double plane_alt,
        double plane_pitch, double plane_roll, double plane_heading);
    void update_buffer_hashes();

    static void CALLBACK dispatch_proc(SIMCONNECT_RECV* pData, DWORD cbData, void* pContext);

    HANDLE simconnect_handle_ = NULL;
    bool connected_ = false;
    bool has_camera_data_ = false;
    bool has_position_data_ = false;

    float z_near_ = 1.0f;
    float z_far_ = 50000.0f;

    static double camera_buffer_[17];
    static std::atomic<double> camera_buffer_counter_;
};
