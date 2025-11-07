#ifdef _WIN32
#define WINVER 0x0500
#define _WIN32_WINNT 0x0500
#include <windows.h>
#endif

#include <assert.h>
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <atomic>
#include <vector>

#include "amtrucks/scssdk_ats.h"
#include "amtrucks/scssdk_telemetry_ats.h"
#include "eurotrucks2/scssdk_eut2.h"
#include "eurotrucks2/scssdk_telemetry_eut2.h"
#include "scssdk_telemetry.h"

#define UNUSED(x)

#pragma pack(push, 1)
struct camera_data_buffer_t {
    double magic_number;  // Magic number for identification: 1.38097189588312856e-12
    double counter;       // Frame counter
    double matrix[12];    // 3x4 camera matrix (row-major)
    double fov;           // Vertical FOV
    double hash1;         // Hash value 1 (sum)
    double hash2;         // Hash value 2 (alternating sum)

    camera_data_buffer_t() {
        magic_number = 1.38097189588312856e-12;
        counter = 1.0;
        for (int i = 0; i < 12; ++i) {
            matrix[i] = 0.0;
        }
        fov = 60.0;
        hash1 = 0.0;
        hash2 = 0.0;
    }
};
#pragma pack(pop)

// Shared memory for camera data
static const char* SHARED_MEMORY_NAME = "SCSTelemetryCameraData";
static HANDLE g_shared_memory_handle = NULL;
static camera_data_buffer_t* g_shared_camera_buffer = NULL;
static std::atomic<bool> g_buffer_initialized{false};

struct telemetry_state_t {
    scs_value_fvector_t cabin_position;
    scs_value_fvector_t head_position;
    scs_value_dplacement_t truck_placement;
    scs_value_fplacement_t cabin_offset;
    scs_value_fplacement_t head_offset;
} telemetry;

bool initialize_shared_memory(void) {
    const DWORD memory_size = sizeof(camera_data_buffer_t);

    g_shared_memory_handle = CreateFileMappingA(
        INVALID_HANDLE_VALUE,
        NULL,
        PAGE_READWRITE | SEC_COMMIT,
        0,
        memory_size,
        SHARED_MEMORY_NAME);

    if (!g_shared_memory_handle) {
        return false;
    }

    g_shared_camera_buffer = static_cast<camera_data_buffer_t*>(
        MapViewOfFile(g_shared_memory_handle, FILE_MAP_ALL_ACCESS, 0, 0, 0));

    if (!g_shared_camera_buffer) {
        CloseHandle(g_shared_memory_handle);
        g_shared_memory_handle = NULL;
        return false;
    }

    // Initialize with default values
    *g_shared_camera_buffer = camera_data_buffer_t();
    g_buffer_initialized.store(true);

    return true;
}

void deinitialize_shared_memory(void) {
    if (g_shared_camera_buffer) {
        UnmapViewOfFile(g_shared_camera_buffer);
        g_shared_camera_buffer = NULL;
    }
    if (g_shared_memory_handle) {
        CloseHandle(g_shared_memory_handle);
        g_shared_memory_handle = NULL;
    }
    g_buffer_initialized.store(false);
}

scs_value_fvector_t add(const scs_value_fvector_t& first, const scs_value_fvector_t& second) {
    scs_value_fvector_t result;
    result.x = first.x + second.x;
    result.y = first.y + second.y;
    result.z = first.z + second.z;
    return result;
}

scs_value_dvector_t add(const scs_value_dvector_t& first, const scs_value_fvector_t& second) {
    scs_value_dvector_t result;
    result.x = first.x + second.x;
    result.y = first.y + second.y;
    result.z = first.z + second.z;
    return result;
}

scs_value_fvector_t rotate(const scs_value_euler_t& orientation, const scs_value_fvector_t& vector) {
    const float heading_radians = orientation.heading * 6.2831853071795864769252867665590058f;
    const float pitch_radians = orientation.pitch * 6.2831853071795864769252867665590058f;
    const float roll_radians = orientation.roll * 6.2831853071795864769252867665590058f;

    const float cos_heading = cosf(heading_radians);
    const float sin_heading = sinf(heading_radians);
    const float cos_pitch = cosf(pitch_radians);
    const float sin_pitch = sinf(pitch_radians);
    const float cos_roll = cosf(roll_radians);
    const float sin_roll = sinf(roll_radians);
    // Z: back
    const float post_roll_x = vector.x * cos_roll - vector.y * sin_roll;
    const float post_roll_y = vector.x * sin_roll + vector.y * cos_roll;
    const float post_roll_z = vector.z;
    // x：right
    const float post_pitch_x = post_roll_x;
    const float post_pitch_y = post_roll_y * cos_pitch - post_roll_z * sin_pitch;
    const float post_pitch_z = post_roll_y * sin_pitch + post_roll_z * cos_pitch;
    // y：up
    scs_value_fvector_t result;
    result.x = post_pitch_x * cos_heading + post_pitch_z * sin_heading;
    result.y = post_pitch_y;
    result.z = -post_pitch_x * sin_heading + post_pitch_z * cos_heading;
    return result;
}

void euler_to_matrix(const scs_value_euler_t& orientation, float matrix[9]) {
    const float h_rad = orientation.heading * 6.2831853071795864769252867665590058f;
    const float p_rad = orientation.pitch * 6.2831853071795864769252867665590058f;
    const float r_rad = orientation.roll * 6.2831853071795864769252867665590058f;

    const float cos_h = cosf(h_rad);
    const float sin_h = sinf(h_rad);
    const float cos_p = cosf(p_rad);
    const float sin_p = sinf(p_rad);
    const float cos_r = cosf(r_rad);
    const float sin_r = sinf(r_rad);

    matrix[0] = cos_h * cos_r + sin_h * sin_p * sin_r;
    matrix[1] = -cos_h * sin_r + sin_h * sin_p * sin_r;
    matrix[2] = sin_h * cos_p;

    matrix[3] = cos_p * sin_r;
    matrix[4] = cos_p * cos_r;
    matrix[5] = -sin_p;

    matrix[6] = -sin_h * cos_r + cos_h * sin_p * sin_r;
    matrix[7] = sin_h * sin_r + cos_h * sin_p * cos_r;
    matrix[8] = cos_h * cos_p;
}

void multiply_matrices(const float a[9], const float b[9], float result[9]) {
    result[0] = a[0] * b[0] + a[1] * b[3] + a[2] * b[6];
    result[1] = a[0] * b[1] + a[1] * b[4] + a[2] * b[7];
    result[2] = a[0] * b[2] + a[1] * b[5] + a[2] * b[8];

    result[3] = a[3] * b[0] + a[4] * b[3] + a[5] * b[6];
    result[4] = a[3] * b[1] + a[4] * b[4] + a[5] * b[7];
    result[5] = a[3] * b[2] + a[4] * b[5] + a[5] * b[8];

    result[6] = a[6] * b[0] + a[7] * b[3] + a[8] * b[6];
    result[7] = a[6] * b[1] + a[7] * b[4] + a[8] * b[7];
    result[8] = a[6] * b[2] + a[7] * b[5] + a[8] * b[8];
}

void rotation_matrix_to_view_matrix(const float rotation_matrix[9], double view_matrix[16]) {
    view_matrix[0] = static_cast<double>(rotation_matrix[0]);
    view_matrix[1] = static_cast<double>(rotation_matrix[1]);
    view_matrix[2] = static_cast<double>(rotation_matrix[2]);

    view_matrix[4] = static_cast<double>(rotation_matrix[3]);
    view_matrix[5] = static_cast<double>(rotation_matrix[4]);
    view_matrix[6] = static_cast<double>(rotation_matrix[5]);

    view_matrix[8] = static_cast<double>(rotation_matrix[6]);
    view_matrix[9] = static_cast<double>(rotation_matrix[7]);
    view_matrix[10] = static_cast<double>(rotation_matrix[8]);

    view_matrix[3] = 0.0;
    view_matrix[7] = 0.0;
    view_matrix[11] = 0.0;
    view_matrix[12] = 0.0;
    view_matrix[13] = 0.0;
    view_matrix[14] = 0.0;
    view_matrix[15] = 1.0;
}

void update_camera_buffer(const scs_value_dvector_t& head_position,
                          const scs_value_euler_t& head_orientation,
                          const scs_value_euler_t& cabin_orientation,
                          const scs_value_euler_t& truck_orientation) {
    if (!g_shared_camera_buffer || !g_buffer_initialized.load()) {
        return;
    }

    g_shared_camera_buffer->counter += 1.0;
    if (g_shared_camera_buffer->counter > 9999.0) {
        g_shared_camera_buffer->counter = 1.0;
    }

    float head_matrix[9], cabin_matrix[9], truck_matrix[9];
    euler_to_matrix(head_orientation, head_matrix);
    euler_to_matrix(cabin_orientation, cabin_matrix);
    euler_to_matrix(truck_orientation, truck_matrix);

    float temp_matrix[9], combined_matrix[9];
    multiply_matrices(truck_matrix, cabin_matrix, temp_matrix);
    multiply_matrices(temp_matrix, head_matrix, combined_matrix);

    rotation_matrix_to_view_matrix(combined_matrix, g_shared_camera_buffer->matrix);

    // Position
    g_shared_camera_buffer->matrix[3] = head_position.x;
    g_shared_camera_buffer->matrix[7] = head_position.y;
    g_shared_camera_buffer->matrix[11] = head_position.z;

    // FOV
    g_shared_camera_buffer->fov = 77.0;

    // Calculate hash values for verification
    g_shared_camera_buffer->hash1 = g_shared_camera_buffer->counter;
    g_shared_camera_buffer->hash2 = g_shared_camera_buffer->counter;

    for (int i = 0; i < 12; ++i) {
        g_shared_camera_buffer->hash1 += g_shared_camera_buffer->matrix[i];
        if (i % 2 == 0) {
            g_shared_camera_buffer->hash2 += g_shared_camera_buffer->matrix[i];
        } else {
            g_shared_camera_buffer->hash2 -= g_shared_camera_buffer->matrix[i];
        }
    }
    g_shared_camera_buffer->hash1 += g_shared_camera_buffer->fov;
    g_shared_camera_buffer->hash2 -= g_shared_camera_buffer->fov;
}

SCSAPI_VOID telemetry_frame_end(const scs_event_t UNUSED(event), const void* const UNUSED(event_info), const scs_context_t UNUSED(context)) {
    if (!g_shared_camera_buffer) return;

    const scs_value_fvector_t head_position_in_cabin_space = add(telemetry.head_position, telemetry.head_offset.position);
    const scs_value_fvector_t head_position_in_vehicle_space = add(add(telemetry.cabin_position, telemetry.cabin_offset.position),
                                                                   rotate(telemetry.cabin_offset.orientation, head_position_in_cabin_space));
    const scs_value_dvector_t head_position_in_world_space = add(telemetry.truck_placement.position,
                                                                 rotate(telemetry.truck_placement.orientation, head_position_in_vehicle_space));
    update_camera_buffer(head_position_in_world_space,
                         telemetry.head_offset.orientation,
                         telemetry.cabin_offset.orientation,
                         telemetry.truck_placement.orientation);
}

SCSAPI_VOID telemetry_pause(const scs_event_t event, const void* const UNUSED(event_info), const scs_context_t UNUSED(context)) {
    // Pause state handling if needed
}

const scs_named_value_t* find_attribute(const scs_telemetry_configuration_t& configuration, const char* const name, const scs_u32_t index, const scs_value_type_t expected_type) {
    for (const scs_named_value_t* current = configuration.attributes; current->name; ++current) {
        if ((current->index != index) || (strcmp(current->name, name) != 0)) {
            continue;
        }
        if (current->value.type == expected_type) {
            return current;
        }
        break;
    }
    return NULL;
}

SCSAPI_VOID telemetry_configuration(const scs_event_t event, const void* const event_info, const scs_context_t UNUSED(context)) {
    const struct scs_telemetry_configuration_t* const info = static_cast<const scs_telemetry_configuration_t*>(event_info);

    if (strcmp(info->id, SCS_TELEMETRY_CONFIG_truck) != 0) {
        return;
    }

    const scs_named_value_t* const cabin_position = find_attribute(*info, SCS_TELEMETRY_CONFIG_ATTRIBUTE_cabin_position, SCS_U32_NIL, SCS_VALUE_TYPE_fvector);
    if (cabin_position) {
        telemetry.cabin_position = cabin_position->value.value_fvector;
    } else {
        telemetry.cabin_position.x = telemetry.cabin_position.y = telemetry.cabin_position.z = 0.0f;
    }

    const scs_named_value_t* const head_position = find_attribute(*info, SCS_TELEMETRY_CONFIG_ATTRIBUTE_head_position, SCS_U32_NIL, SCS_VALUE_TYPE_fvector);
    if (head_position) {
        telemetry.head_position = head_position->value.value_fvector;
    } else {
        telemetry.head_position.x = telemetry.head_position.y = telemetry.head_position.z = 0.0f;
    }
}

SCSAPI_VOID telemetry_store_fplacement(const scs_string_t name, const scs_u32_t index, const scs_value_t* const value, const scs_context_t context) {
    assert(context);
    assert(value);
    assert(value->type == SCS_VALUE_TYPE_fplacement);
    scs_value_fplacement_t* const placement = static_cast<scs_value_fplacement_t*>(context);
    *placement = value->value_fplacement;
}

SCSAPI_VOID telemetry_store_dplacement(const scs_string_t name, const scs_u32_t index, const scs_value_t* const value, const scs_context_t context) {
    assert(context);
    assert(value);
    assert(value->type == SCS_VALUE_TYPE_dplacement);
    scs_value_dplacement_t* const placement = static_cast<scs_value_dplacement_t*>(context);
    *placement = value->value_dplacement;
}

SCSAPI_RESULT scs_telemetry_init(const scs_u32_t version, const scs_telemetry_init_params_t* const params) {
    if (version != SCS_TELEMETRY_VERSION_1_00) {
        return SCS_RESULT_unsupported;
    }

    const scs_telemetry_init_params_v100_t* const version_params = static_cast<const scs_telemetry_init_params_v100_t*>(params);

    if (!initialize_shared_memory()) {
        return SCS_RESULT_generic_error;
    }

    const bool events_registered =
        (version_params->register_for_event(SCS_TELEMETRY_EVENT_frame_end, telemetry_frame_end, NULL) == SCS_RESULT_ok) &&
        (version_params->register_for_event(SCS_TELEMETRY_EVENT_paused, telemetry_pause, NULL) == SCS_RESULT_ok) &&
        (version_params->register_for_event(SCS_TELEMETRY_EVENT_started, telemetry_pause, NULL) == SCS_RESULT_ok) &&
        (version_params->register_for_event(SCS_TELEMETRY_EVENT_configuration, telemetry_configuration, NULL) == SCS_RESULT_ok);

    if (!events_registered) {
        deinitialize_shared_memory();
        return SCS_RESULT_generic_error;
    }

    version_params->register_for_channel(SCS_TELEMETRY_TRUCK_CHANNEL_world_placement, SCS_U32_NIL, SCS_VALUE_TYPE_dplacement, SCS_TELEMETRY_CHANNEL_FLAG_none, telemetry_store_dplacement, &telemetry.truck_placement);
    version_params->register_for_channel(SCS_TELEMETRY_TRUCK_CHANNEL_cabin_offset, SCS_U32_NIL, SCS_VALUE_TYPE_fplacement, SCS_TELEMETRY_CHANNEL_FLAG_none, telemetry_store_fplacement, &telemetry.cabin_offset);
    version_params->register_for_channel(SCS_TELEMETRY_TRUCK_CHANNEL_head_offset, SCS_U32_NIL, SCS_VALUE_TYPE_fplacement, SCS_TELEMETRY_CHANNEL_FLAG_none, telemetry_store_fplacement, &telemetry.head_offset);

    memset(&telemetry, 0, sizeof(telemetry));
    return SCS_RESULT_ok;
}

SCSAPI_VOID scs_telemetry_shutdown(void) {
    deinitialize_shared_memory();
}

#ifdef _WIN32
BOOL APIENTRY DllMain(HMODULE module, DWORD reason_for_call, LPVOID reserved) {
    if (reason_for_call == DLL_PROCESS_DETACH) {
        deinitialize_shared_memory();
    }
    return TRUE;
}
#endif