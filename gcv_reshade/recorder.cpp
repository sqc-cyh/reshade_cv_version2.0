#include "recorder.h"
#include <reshade.hpp>
#include <cstring>
#include <ShlObj.h>
#include <nlohmann/json.hpp>
#include <mutex>
#include <sstream>
#include "H5Cpp.h"
using Json = nlohmann::json_abi_v3_12_0::json;
static inline std::string join_path_slash(std::string s) {
  if (!s.empty() && s.back()!='/' && s.back()!='\\') s.push_back('/');
  return s;
}

static inline void ensure_dir_existsA(const std::string& dir) {
  std::string d = dir;
  for (auto &ch : d) if (ch == '/') ch = '\\';
  if (!d.empty() && d.back() != '\\') d.push_back('\\');
  SHCreateDirectoryExA(nullptr, d.c_str(), nullptr);  
}

Recorder::Recorder(const RecorderConfig& cfg)
    : cfg_(cfg), group_counter_(0)
{
    InitializeCriticalSection(&depth_cs_);
    ring_c_.resize(cap_c_);
    ring_d_.resize(cap_d_);

    // 启动 HDF5 写入线程
    h5_thread_ = std::thread(&Recorder::h5_write_thread, this);
}

Recorder::~Recorder() {
    // 停止 HDF5 线程
    h5_thread_run_ = false;
    h5_cv_.notify_one();
    if (h5_thread_.joinable()) {
        h5_thread_.join();
    }

    DeleteCriticalSection(&depth_cs_);
    stop();
}


bool Recorder::start(){
  if (running_) return true;
  running_ = true;

  // ensure the output directory exists (must!!)
  const std::string out_dir_norm = join_path_slash(cfg_.out_dir);
  ensure_dir_existsA(out_dir_norm);

  // open csv
  if (cfg_.write_csv) {
    const std::string csv_path = out_dir_norm + "actions.csv";
    csv_ = _fsopen(csv_path.c_str(), "w", _SH_DENYNO);
    if (csv_) {
      setvbuf(csv_, nullptr, _IONBF, 0);
      std::fprintf(csv_, "frame_idx,time_us,w,a,s,d,shift,space\n");
    } else {
      // error log
      int e = errno;
      DWORD we = GetLastError();
      char buf[512];
      _snprintf_s(buf, _TRUNCATE,
        "[CV Capture] open actions.csv failed: path=%s errno=%d winerr=%lu",
        csv_path.c_str(), e, (unsigned long)we);
      reshade::log_message(reshade::log_level::error, buf);
    }
  }
  try {
    cam_jsonl_.open(out_dir_norm + "cam.jsonl", std::ios::out | std::ios::trunc);
      if (!cam_jsonl_.is_open()) {
        reshade::log_message(reshade::log_level::error, "[CV Capture] open cam.jsonl failed");
      }
  } catch (...) {
      reshade::log_message(reshade::log_level::error, "[CV Capture] exception opening cam.jsonl");
  }
  return true;
}

void Recorder::stop(){
  if (!running_) return;
  running_ = false;

  // stop thread
  if (th_run_c_.exchange(false)) { if (th_c_.joinable()) th_c_.join(); }
  if (th_run_d_.exchange(false)) { if (th_d_.joinable()) th_d_.join(); }

  // stop pipe
  pipe_c_.stop();
  pipe_d_.stop();

  if (!depth_cache_.empty()) {
      reshade::log_message(reshade::log_level::info,
          "[CV Capture] Saving last partial depth group");
      save_depth_group_to_h5();  // 即使不满 5 帧也保存
  }
  if (csv_) { fclose(csv_); csv_ = nullptr; }
  if (cam_jsonl_.is_open()) { cam_jsonl_.close(); }
  char s[128];
  _snprintf_s(s, _TRUNCATE, "[CV Capture] frames enqueued=%llu, written=%llu",
              (unsigned long long)enqueued_.load(), (unsigned long long)written_.load());
  reshade::log_message(reshade::log_level::info, s);
}

bool Recorder::q_push(std::vector<RawFrame>& Q, std::atomic<uint32_t>& P, std::atomic<uint32_t>& C, RawFrame&& f, size_t cap){
  uint32_t p = P.load(std::memory_order_relaxed);
  uint32_t c = C.load(std::memory_order_acquire);
  if ((p - c) >= cap) return false;
  Q[p % cap] = std::move(f);
  P.store(p+1, std::memory_order_release);
  enqueued_.fetch_add(1, std::memory_order_relaxed);
  return true;
}
bool Recorder::q_pop(std::vector<RawFrame>& Q, std::atomic<uint32_t>& P, std::atomic<uint32_t>& C, RawFrame& out, size_t cap){
  uint32_t c = C.load(std::memory_order_relaxed);
  uint32_t p = P.load(std::memory_order_acquire);
  if (c == p) return false;
  out = std::move(Q[c % cap]);
  C.store(c+1, std::memory_order_release);
  return true;
}

void Recorder::ensure_color_started(int w,int h){
  if (!cfg_.write_video) return;
  if (pipe_c_.alive()) return;
  if (!pipe_c_.start_bgra(w, h, cfg_.fps, cfg_.out_dir)) {
    reshade::log_message(reshade::log_level::error, "ffmpeg start failed; stop color stream");
    return;
  }
  if (!th_run_c_.load()) {
    th_run_c_ = true;
    th_c_ = std::thread(&Recorder::color_loop, this);
  }
}
void Recorder::ensure_depth_started(int w,int h){
  if (!cfg_.write_video) return;
  if (pipe_d_.alive()) return;
  if (!pipe_d_.start_gray(w, h, cfg_.fps, cfg_.out_dir)) {
    reshade::log_message(reshade::log_level::error, "ffmpeg start (depth) failed");
    return;
  }
  if (!th_run_d_.load()) {
    th_run_d_ = true;
    th_d_ = std::thread(&Recorder::depth_loop, this);
  }
}

void Recorder::push_color(const uint8_t* bgra,int w,int h){
  if (!running_ || !bgra || w<=0 || h<=0) return;
  ensure_color_started(w,h);

  RawFrame f; f.w=w; f.h=h; f.stride=(size_t)w*4; f.size=f.stride*(size_t)h;
  f.data.reset(new uint8_t[f.size]);
  std::memcpy(f.data.get(), bgra, f.size);
  (void)q_push(ring_c_, prod_c_, cons_c_, std::move(f), cap_c_);

  last_bgra_.assign(bgra, bgra + (size_t)w*h*4);
  lw_=w; lh_=h;
}

void Recorder::push_depth(const uint8_t* gray,int w,int h){
  if (!running_ || !gray || w<=0 || h<=0) return;
  ensure_depth_started(w,h);

  RawFrame f; f.w=w; f.h=h; f.stride=(size_t)w; f.size=f.stride*(size_t)h;
  f.data.reset(new uint8_t[f.size]);
  std::memcpy(f.data.get(), gray, f.size);
  (void)q_push(ring_d_, prod_d_, cons_d_, std::move(f), cap_d_);

  last_gray_.assign(gray, gray + (size_t)w*h);
  dw_=w; dh_=h;
}

void Recorder::push_raw_depth(const float* data, int width, int height, uint64_t frame_idx, int64_t timestamp_us){
    if (!running_ || !data || width <= 0 || height <= 0) return;

    const size_t num_pixels = (size_t)width * height;
    std::vector<float> copy(data, data + num_pixels);

    EnterCriticalSection(&depth_cs_);
    depth_cache_.push_back({
        std::move(copy),
        width, height,
        frame_idx,
        timestamp_us
    });

    bool should_save = (depth_cache_.size() >= group_size_);
    LeaveCriticalSection(&depth_cs_);

    if (should_save) {
        save_depth_group_to_h5();  // 现在是异步的！
    }
}


void Recorder::save_depth_group_to_h5() {
    if (depth_cache_.empty()) return;

    EnterCriticalSection(&depth_cs_);

    DepthGroup group;
    group.T = depth_cache_.size();
    group.H = depth_cache_[0].height;
    group.W = depth_cache_[0].width;
    group.frame_start = depth_cache_.front().frame_idx;
    group.frame_end = depth_cache_.back().frame_idx;
    group.ts_start = depth_cache_.front().timestamp_us;
    group.ts_end = depth_cache_.back().timestamp_us;
    group.group_id = group_counter_++;
    group.out_dir = cfg_.out_dir;
    group.fps = cfg_.fps;

    group.all_data.reserve(group.T * group.H * group.W);
    for (const auto& frame : depth_cache_) {
        group.all_data.insert(group.all_data.end(), frame.data.begin(), frame.data.end());
    }

    depth_cache_.clear();
    LeaveCriticalSection(&depth_cs_);

    // 放入异步队列
    {
        std::lock_guard<std::mutex> lk(h5_mutex_);
        h5_queue_.push(std::move(group));
    }
    h5_cv_.notify_one();
}


void Recorder::h5_write_thread() {
    while (h5_thread_run_.load()) {
        std::unique_lock<std::mutex> lk(h5_mutex_);
        h5_cv_.wait(lk, [this] {
            return !h5_queue_.empty() || !h5_thread_run_;
        });

        if (!h5_thread_run_ && h5_queue_.empty()) break;

        DepthGroup group = std::move(h5_queue_.front());
        h5_queue_.pop();
        lk.unlock();

        try {
            std::stringstream ss;
            ss << group.out_dir << "/depth_group_"
               << std::setfill('0') << std::setw(6) << group.group_id << ".h5";

            H5::H5File file(ss.str().c_str(), H5F_ACC_TRUNC);
            hsize_t dims[3] = {(hsize_t)group.T, (hsize_t)group.H, (hsize_t)group.W};
            H5::DataSpace space(3, dims);

            H5::DSetCreatPropList plist;
            
            // 每帧一个 chunk，提升压缩率，服了没啥用
            // hsize_t chunk_dims[3] = {1, (hsize_t)group.H, (hsize_t)group.W};
            plist.setChunk(3, dims);
            plist.setDeflate(6);

            H5::DataSet dataset = file.createDataSet("/depth", H5::PredType::NATIVE_FLOAT, space, plist);
            dataset.write(group.all_data.data(), H5::PredType::NATIVE_FLOAT);

            auto write_attr = [&](const char* name, uint64_t value) {
                H5::DataSpace attr_space(H5S_SCALAR);
                H5::Attribute attr = dataset.createAttribute(name, H5::PredType::NATIVE_UINT64, attr_space);
                attr.write(H5::PredType::NATIVE_UINT64, &value);
            };

            write_attr("frame_start_idx", group.frame_start);
            write_attr("frame_end_idx", group.frame_end);
            write_attr("timestamp_start_us", group.ts_start);
            write_attr("timestamp_end_us", group.ts_end);
            write_attr("num_frames", group.T);
            write_attr("fps", group.fps);

            file.close();

            char logbuf[256];
            _snprintf_s(logbuf, _TRUNCATE,
                "[CV Capture] Saved %d depth frames to %s", group.T, ss.str().c_str());
            reshade::log_message(reshade::log_level::info, logbuf);

        } catch (...) {
            reshade::log_message(reshade::log_level::error, "[HDF5] Write failed in thread");
        }
    }
}



void Recorder::duplicate(int n){
  if (n<=0) return;
  for (int i=0;i<n;++i){
    if (pipe_c_.alive() && lw_>0 && lh_>0){
      RawFrame f; f.w=lw_; f.h=lh_; f.stride=(size_t)lw_*4; f.size=f.stride*(size_t)lh_;
      f.data.reset(new uint8_t[f.size]);
      std::memcpy(f.data.get(), last_bgra_.data(), f.size);
      (void)q_push(ring_c_, prod_c_, cons_c_, std::move(f), cap_c_);
    }
    if (pipe_d_.alive() && dw_>0 && dh_>0){
      RawFrame f; f.w=dw_; f.h=dh_; f.stride=(size_t)dw_; f.size=f.stride*(size_t)dh_;
      f.data.reset(new uint8_t[f.size]);
      std::memcpy(f.data.get(), last_gray_.data(), f.size);
      (void)q_push(ring_d_, prod_d_, cons_d_, std::move(f), cap_d_);
    }
  }
}

void Recorder::log_action(uint64_t idx, long long t_us, uint32_t keymask){
  if (!csv_) return;
  auto b = [&](unsigned bit){ return (keymask & (1u<<bit)) ? 1 : 0; };
  std::fprintf(csv_, "%llu,%lld,%d,%d,%d,%d,%d,%d\n",
    (unsigned long long)idx, (long long)t_us,
    b(0), b(1), b(2), b(3), b(4), b(5));
}

void Recorder::color_loop(){
  RawFrame f;
  while (th_run_c_.load(std::memory_order_acquire)){
    if (!q_pop(ring_c_, prod_c_, cons_c_, f, cap_c_)) { Sleep(1); continue; }
    if (f.data && f.size && pipe_c_.alive() && pipe_c_.hWrite()){
      if (!pipe_c_.write(f.data.get(), f.size)) {
        reshade::log_message(reshade::log_level::error, "[CV Capture] Write color frame failed");
        th_run_c_.store(false, std::memory_order_release); break;
      } else {
        written_.fetch_add(1, std::memory_order_relaxed);
      }
    }
    f = {};
  }
}

void Recorder::depth_loop(){
  RawFrame f;
  while (th_run_d_.load(std::memory_order_acquire)){
    if (!q_pop(ring_d_, prod_d_, cons_d_, f, cap_d_)) { Sleep(1); continue; }
    if (f.data && f.size && pipe_d_.alive() && pipe_d_.hWrite()){
      if (!pipe_d_.write(f.data.get(), f.size)) {
        reshade::log_message(reshade::log_level::error, "[CV Capture] Write depth frame failed");
        th_run_d_.store(false, std::memory_order_release); break;
      }
    }
    f = {};
  }
}

// write to cam.jsonl
void Recorder::log_camera_json(uint64_t idx, long long t_us,
                               const Json& cam_json,
                               int img_w, int img_h)
{
  if (!running_) return;

  try {
    Json j = cam_json;       
    j["frame_idx"] = idx;
    j["time_us"]   = t_us;
    j["img_w"]     = img_w;
    j["img_h"]     = img_h;

    if (cam_jsonl_.is_open()) {
      cam_jsonl_ << j.dump() << '\n';
      cam_jsonl_.flush();
    }

    // 每帧写一份 camera.json
    const std::string out_dir_norm = join_path_slash(cfg_.out_dir);
    char namebuf[128];
    _snprintf_s(namebuf, _TRUNCATE, "frame_%06llu_camera.json",
                (unsigned long long)idx);
    const std::string per_frame_path = out_dir_norm + namebuf;

    std::ofstream jf(per_frame_path, std::ios::out | std::ios::trunc);
    if (jf.is_open() && jf.good()) {
      jf << j.dump() << std::endl;
      jf.close();
    } else {
      reshade::log_message(reshade::log_level::warning,
        "[CV Capture] failed to write per-frame camera.json");
    }
  } catch (...) {
    reshade::log_message(reshade::log_level::error,
      "[CV Capture] exception writing camera json");
  }
}