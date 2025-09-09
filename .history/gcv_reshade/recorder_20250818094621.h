#pragma once
#include <cstdint>
#include <cstdio>
#include <thread>
#include <atomic>
#include <vector>
#include <memory>
#include <string>

#include "ffmpeg_pipe_win.h"
#include <fstream>


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

class Recorder {
public:
  explicit Recorder(const RecorderConfig& cfg);
  ~Recorder();

  bool start();
  void stop();
  bool running() const { return running_; }

  void push_color(const uint8_t* bgra, int w, int h);
  void push_depth(const uint8_t* gray, int w, int h);

  // Add n frames from the previous frame (for cfr alignment)
  void duplicate(int n_dup);

  // record action
  void log_action(uint64_t idx, long long t_us, uint32_t keymask);
  void log_camera_json(uint64_t idx, long long t_us,
                       const nlohmann::json& cam_json,
                       int img_w, int img_h);
private:
  // ring queue
  bool q_push(std::vector<RawFrame>& Q, std::atomic<uint32_t>& P, std::atomic<uint32_t>& C, RawFrame&& f, size_t cap);
  bool q_pop (std::vector<RawFrame>& Q, std::atomic<uint32_t>& P, std::atomic<uint32_t>& C, RawFrame& out, size_t cap);

  void color_loop();
  void depth_loop();
  void ensure_color_started(int w,int h);
  void ensure_depth_started(int w,int h);

  RecorderConfig cfg_;
  std::atomic<bool> running_{false};

  // two rings
  std::vector<RawFrame> ring_c_; std::atomic<uint32_t> prod_c_{0}, cons_c_{0};
  std::vector<RawFrame> ring_d_; std::atomic<uint32_t> prod_d_{0}, cons_d_{0};
  const size_t cap_c_ = 8, cap_d_ = 8;

  std::atomic<bool> th_run_c_{false}, th_run_d_{false};
  std::thread th_c_, th_d_;
  FfmpegPipe pipe_c_, pipe_d_;

  // The recent frame cache is used for duplicate
  std::vector<uint8_t> last_bgra_, last_gray_;
  int lw_=0, lh_=0, dw_=0, dh_=0;

  FILE* csv_{nullptr};
  std::atomic<uint64_t> enqueued_{0}, written_{0};
  std::ofstream cam_jsonl_;
};
