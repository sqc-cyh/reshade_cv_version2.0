#pragma once

#include <cstdint>
#include <cstdio>
#include <thread>
#include <atomic>
#include <vector>
#include <memory>
#include <string>
#include <mutex>
#include <queue>              // ✅ 必须加！
#include <condition_variable> // ✅ 异步队列需要
#include "ffmpeg_pipe_win.h"
#include <fstream>
#include <nlohmann/json_fwd.hpp>

using Json = nlohmann::json;

// 前向声明
struct DepthFrame;
class Recorder;

// 数据结构
struct RawFrame {
    std::unique_ptr<uint8_t[]> data;
    size_t size = 0, stride = 0;
    int w = 0, h = 0;
};
using RawFrameGray = RawFrame;

struct RecorderConfig {
    int fps = 30;
    std::string out_dir;      
    bool write_video = true;  
    bool write_csv = true;    
};

struct DepthFrame {  
    std::vector<float> data;
    int width, height;
    uint64_t frame_idx;
    int64_t timestamp_us;
};

// 异步写入的数据块
struct DepthGroup {
    std::vector<float> all_data;
    int T, H, W;
    uint64_t group_id;
    uint64_t frame_start, frame_end;
    int64_t ts_start, ts_end;
    std::string out_dir;
    int fps;
};

class Recorder {
public:
    explicit Recorder(const RecorderConfig& cfg);
    ~Recorder();

    bool start();
    void stop();
    bool running() const { return running_; }

    void push_color(const uint8_t* bgra, int w, int h);
    void push_depth(const uint8_t* gray, int w, int h);
    void push_raw_depth(const float* data, int w, int h, uint64_t frame_idx, int64_t timestamp_us);
    void duplicate(int n_dup);

    void log_action(uint64_t idx, long long t_us, uint32_t keymask);
    void log_camera_json(uint64_t idx, long long t_us, const Json& cam_json, int img_w, int img_h);

private:
    bool q_push(std::vector<RawFrame>& Q, std::atomic<uint32_t>& P, std::atomic<uint32_t>& C, RawFrame&& f, size_t cap);
    bool q_pop (std::vector<RawFrame>& Q, std::atomic<uint32_t>& P, std::atomic<uint32_t>& C, RawFrame& out, size_t cap);

    void color_loop();
    void depth_loop();
    void ensure_color_started(int w, int h);
    void ensure_depth_started(int w, int h);

    void save_depth_group_to_h5();  // ✅ 声明函数
    void h5_write_thread();         // ✅ 声明线程函数

private:
    RecorderConfig cfg_;
    std::atomic<bool> running_{false};

    // 队列
    std::vector<RawFrame> ring_c_;
    std::vector<RawFrame> ring_d_;
    const size_t cap_c_ = 8, cap_d_ = 8;
    std::atomic<uint32_t> prod_c_{0}, cons_c_{0};
    std::atomic<uint32_t> prod_d_{0}, cons_d_{0};

    // 线程与管道
    std::atomic<bool> th_run_c_{false}, th_run_d_{false};
    std::thread th_c_, th_d_;
    FfmpegPipe pipe_c_, pipe_d_;

    // 最近帧缓存
    std::vector<uint8_t> last_bgra_, last_gray_;
    int lw_ = 0, lh_ = 0, dw_ = 0, dh_ = 0;

    // CSV & JSONL
    FILE* csv_{nullptr};
    std::atomic<uint64_t> enqueued_{0}, written_{0};
    std::ofstream cam_jsonl_;

    // HDF5 异步相关
    std::vector<DepthFrame> depth_cache_;      // 临时缓存
    mutable CRITICAL_SECTION depth_cs_;        // 保护 depth_cache_
    uint64_t group_counter_ = 0;              // 文件编号
    static const int group_size_ = 30;         // 每组 30 帧

    // 异步写入队列
    std::queue<DepthGroup> h5_queue_;
    std::mutex h5_mutex_;
    std::condition_variable h5_cv_;
    std::atomic<bool> h5_thread_run_{true};
    std::thread h5_thread_;
};
