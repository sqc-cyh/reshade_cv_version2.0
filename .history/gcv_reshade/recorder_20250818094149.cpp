#include "recorder.h"
#include <reshade.hpp>
#include <cstring>
#include <ShlObj.h>
#include <nlohmann/json.hpp>

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

Recorder::Recorder(const RecorderConfig& cfg): cfg_(cfg){
  ring_c_.resize(cap_c_);
  ring_d_.resize(cap_d_);
}
Recorder::~Recorder(){ stop(); }

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

  if (csv_) { fclose(csv_); csv_ = nullptr; }

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
