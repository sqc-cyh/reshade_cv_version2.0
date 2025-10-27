#include "EuroTruckSimulator2.h"

#include <Windows.h>

#include "gcv_utils/depth_utils.h"
#include "gcv_utils/scripted_cam_buf_templates.h"

#pragma pack(push, 1)
struct camera_data_buffer_t {
    double magic_number;  // Magic number for identification: 1.38097189588312856e-12
    double counter;       // Frame counter
    double matrix[12];    // 3x4 camera matrix (row-major)
    double fov;           // Vertical FOV
    double hash1;         // Hash value 1 (sum)
    double hash2;         // Hash value 2 (alternating sum)
};
#pragma pack(pop)

std::string GameEuroTruckSimulator2::gamename_verbose() const { return "Euro Truck Simulator 2"; }

std::string GameEuroTruckSimulator2::camera_dll_name() const { return ""; }
uint64_t GameEuroTruckSimulator2::camera_dll_mem_start() const { return 0; }
GameCamDLLMatrixType GameEuroTruckSimulator2::camera_dll_matrix_format() const {
    return GameCamDLLMatrix_allmemscanrequiredtofindscriptedcambuf;
}

scriptedcam_checkbuf_funptr GameEuroTruckSimulator2::get_scriptedcambuf_checkfun() const {
    return template_check_scriptedcambuf_hash<double, 13, 1>;
}

uint64_t GameEuroTruckSimulator2::get_scriptedcambuf_sizebytes() const {
    return template_scriptedcambuf_sizebytes<double, 13, 1>();
}

bool GameEuroTruckSimulator2::copy_scriptedcambuf_to_matrix(uint8_t* buf, uint64_t buflen, CamMatrixData& rcam, std::string& errstr) const {
    return template_copy_scriptedcambuf_extrinsic_cam2world_and_fov<double, 13, 1>(buf, buflen, rcam, true, errstr);
}

bool GameEuroTruckSimulator2::can_interpret_depth_buffer() const {
    return true;
}

float GameEuroTruckSimulator2::convert_to_physical_distance_depth_u64(uint64_t depthval) const {
    float normalized_depth = static_cast<float>(depthval) / 16777215.0;
    const float z_near = 1.0f;
    const float z_far = 1000.0f;
    const float linear_depth = z_near * z_far / (z_far - normalized_depth * (z_far - z_near));
    return linear_depth;
}
// Shared memory access for camera data
class SharedMemoryCameraReader {
   private:
    HANDLE shared_memory_handle;
    void* shared_memory_view;
    const char* memory_name;

   public:
    SharedMemoryCameraReader(const char* name = "SCSTelemetryCameraData")
        : memory_name(name), shared_memory_handle(NULL), shared_memory_view(NULL) {
    }

    ~SharedMemoryCameraReader() {
        close();
    }

    bool open() {
        shared_memory_handle = OpenFileMappingA(FILE_MAP_READ, FALSE, memory_name);
        if (!shared_memory_handle) {
            return false;
        }

        shared_memory_view = MapViewOfFile(shared_memory_handle, FILE_MAP_READ, 0, 0, 0);
        return shared_memory_view != NULL;
    }

    void close() {
        if (shared_memory_view) {
            UnmapViewOfFile(shared_memory_view);
            shared_memory_view = NULL;
        }
        if (shared_memory_handle) {
            CloseHandle(shared_memory_handle);
            shared_memory_handle = NULL;
        }
    }

    template <typename T>
    const T* get_data() const {
        return static_cast<const T*>(shared_memory_view);
    }

    bool is_valid() const {
        return shared_memory_view != NULL;
    }
};

bool GameEuroTruckSimulator2::get_camera_matrix(CamMatrixData& rcam, std::string& errstr) {
    static SharedMemoryCameraReader shared_memory_reader;

    if (!shared_memory_reader.is_valid()) {
        if (!shared_memory_reader.open()) {
            errstr = "Failed to open shared memory for camera data";
            rcam.extrinsic_status = CamMatrix_Uninitialized;
            return false;
        }
    }

    const camera_data_buffer_t* camera_data = shared_memory_reader.get_data<camera_data_buffer_t>();
    if (!camera_data) {
        errstr = "Failed to get camera data from shared memory";
        rcam.extrinsic_status = CamMatrix_Uninitialized;
        return false;
    }

    // Verify magic number
    const double expected_magic = 1.38097189588312856e-12;
    if (std::abs(camera_data->magic_number - expected_magic) > 1e-20) {
        errstr = "Invalid magic number in shared memory";
        rcam.extrinsic_status = CamMatrix_Uninitialized;
        return false;
    }

    // Convert the camera data to matrix format
    std::vector<uint8_t> buffer(sizeof(camera_data_buffer_t));
    memcpy(buffer.data(), camera_data, sizeof(camera_data_buffer_t));

    bool success = copy_scriptedcambuf_to_matrix(buffer.data(), buffer.size(), rcam, errstr);

    if (success) {
        rcam.extrinsic_status = CamMatrix_AllGood;
    } else {
        rcam.extrinsic_status = CamMatrix_Uninitialized;
    }

    return success;
}

bool GameEuroTruckSimulator2::scan_all_memory_for_scripted_cam_matrix(std::string& errstr) {
    // For shared memory approach, we don't need to scan memory
    // The camera data is directly available via shared memory
    SharedMemoryCameraReader reader;
    if (reader.open()) {
        reader.close();
        cam_matrix_mem_loc_saved = 1;  // Mark as found
        return true;
    }

    errstr = "Shared memory not available for camera data";
    return false;
}