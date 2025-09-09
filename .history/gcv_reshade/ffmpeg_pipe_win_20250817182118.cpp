#include "ffmpeg_pipe_win.h"
#include <process.h>
#include <ShlObj.h>
#include <sstream>
#include <cstdio>
#include <cstring>
#include <reshade.hpp>

// 目录确保存在（与原实现一致）
static void ensure_dir_existsA(const std::string& dir) {
  std::string d = dir;
  for (auto& ch : d) if (ch == '/') ch = '\\';
  if (!d.empty() && d.back() != '\\') d.push_back('\\');
  SHCreateDirectoryExA(nullptr, d.c_str(), nullptr);
}

bool FfmpegPipe::start_cmd(const std::string &cmdline, const std::string& out_dir_raw) {
  SECURITY_ATTRIBUTES sa{ sizeof(SECURITY_ATTRIBUTES) };
  sa.bInheritHandle = TRUE; sa.lpSecurityDescriptor = NULL;

  std::string out_dir = out_dir_raw;
  for (auto &ch : out_dir) if (ch == '/') ch = '\\';
  if (!out_dir.empty() && out_dir.back() != '\\') out_dir.push_back('\\');
  ensure_dir_existsA(out_dir);

  if (!CreatePipe(&hRead_, &hWrite_, &sa, 1<<20)) return false;
  SetHandleInformation(hWrite_, HANDLE_FLAG_INHERIT, 0);

  if (!CreatePipe(&hErrRead_, &hErrWrite_, &sa, 1<<15)) {
    CloseHandle(hRead_); CloseHandle(hWrite_);
    hRead_ = hWrite_ = NULL; return false;
  }
  SetHandleInformation(hErrRead_, HANDLE_FLAG_INHERIT, 0);

  STARTUPINFOA si{}; si.cb = sizeof(si);
  si.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
  si.wShowWindow = SW_HIDE;
  si.hStdInput  = hRead_;
  si.hStdError  = hErrWrite_;
  si.hStdOutput = GetStdHandle(STD_OUTPUT_HANDLE);

  ZeroMemory(&pi_, sizeof(pi_));

  BOOL ok = CreateProcessA(
      NULL, (LPSTR)cmdline.c_str(),
      NULL, NULL, TRUE, CREATE_NO_WINDOW,
      NULL, NULL, &si, &pi_
  );

  CloseHandle(hRead_);  hRead_  = NULL;
  CloseHandle(hErrWrite_); hErrWrite_ = NULL;

  if (!ok) {
    CloseHandle(hWrite_); hWrite_ = NULL;
    CloseHandle(hErrRead_); hErrRead_ = NULL;
    return false;
  }

  hProc_ = pi_.hProcess;

  // 秒退检测
  Sleep(120);
  DWORD code = 0;
  if (!GetExitCodeProcess(pi_.hProcess, &code) || code != STILL_ACTIVE) {
    char buf[512];
    for (;;) {
      DWORD got = 0;
      if (!ReadFile(hErrRead_, buf, sizeof(buf)-1, &got, nullptr) || got==0) break;
      buf[got]=0;
      reshade::log_message(reshade::log_level::error, std::string("[ffmpeg] ").append(buf).c_str());
    }
    if (pi_.hThread) { CloseHandle(pi_.hThread); pi_.hThread=NULL; }
    if (pi_.hProcess){ CloseHandle(pi_.hProcess); pi_.hProcess=NULL; }
    if (hWrite_) { CloseHandle(hWrite_); hWrite_=NULL; }
    if (hErrRead_) { CloseHandle(hErrRead_); hErrRead_=NULL; }
    hProc_ = NULL;
    return false;
  }

  // 异步读取 stderr，打印到 ReShade 日志
  _beginthreadex(nullptr, 0, [](void* arg)->unsigned {
      auto *self = (FfmpegPipe*)arg; char buf[512];
      for (;;) {
        DWORD got = 0;
        if (!ReadFile(self->hErrRead_, buf, sizeof(buf)-1, &got, nullptr) || got==0) break;
        buf[got] = 0;
        reshade::log_message(reshade::log_level::error, std::string("[ffmpeg] ").append(buf).c_str());
      }
      return 0u;
  }, this, 0, nullptr);

  return true;
}

bool FfmpegPipe::start_bgra(int width,int height,int fps,const std::string& outdir_raw){
  std::string outdir = outdir_raw;
  for (auto &ch : outdir) if (ch == '/') ch = '\\';
  if (!outdir.empty() && outdir.back() != '\\') outdir.push_back('\\');
  const std::string out_mp4 = outdir + "capture.mp4";

  std::ostringstream cmd;
  cmd << "ffmpeg -loglevel error -y "
      << "-re "
      << "-f rawvideo -pix_fmt bgra "
      << "-s " << width << "x" << height << " "
      << "-framerate " << fps << " "
      << "-i pipe:0 "
      << "-vsync cfr -r " << fps << " "
      << "-c:v libx264 -preset veryfast -crf 18 "
      << "-pix_fmt yuv420p -movflags +faststart "
      << "\"" << out_mp4 << "\"";
  return start_cmd(cmd.str(), outdir);
}

bool FfmpegPipe::start_gray(int width,int height,int fps,const std::string& outdir_raw){
  std::string outdir = outdir_raw;
  for (auto &ch : outdir) if (ch == '/') ch = '\\';
  if (!outdir.empty() && outdir.back() != '\\') outdir.push_back('\\');
  const std::string out_mp4 = outdir + "depth.mp4";

  std::ostringstream cmd;
  cmd << "ffmpeg -loglevel error -y "
      << "-re "
      << "-f rawvideo -pix_fmt gray "
      << "-s " << width << "x" << height << " "
      << "-framerate " << fps << " "
      << "-i pipe:0 "
      << "-vsync cfr -r " << fps << " "
      << "-c:v libx264 -preset veryfast -crf 18 "
      << "-pix_fmt yuv420p "
      << "\"" << out_mp4 << "\"";
  return start_cmd(cmd.str(), outdir);
}

bool FfmpegPipe::write(const void* data, size_t bytes){
  if (!hProc_ || !hWrite_ || !data || bytes==0) return false;
  DWORD wrote = 0;
  if (!WriteFile(hWrite_, data, (DWORD)bytes, &wrote, nullptr)) return false;
  return wrote == bytes;
}

void FfmpegPipe::stop(){
  if (hWrite_) { CloseHandle(hWrite_); hWrite_ = NULL; }
  if (hErrRead_) { CloseHandle(hErrRead_); hErrRead_ = NULL; }
  if (hErrWrite_){ CloseHandle(hErrWrite_); hErrWrite_ = NULL; }
  if (pi_.hThread) { CloseHandle(pi_.hThread); pi_.hThread = NULL; }
  if (pi_.hProcess){ CloseHandle(pi_.hProcess); pi_.hProcess = NULL; }
  hProc_ = NULL;
}
