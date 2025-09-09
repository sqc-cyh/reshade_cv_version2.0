#pragma once
#include <string>
#include <Windows.h>

class FfmpegPipe {
public:
  // capture.mp4 (BGRA stream)
  bool start_bgra(int width, int height, int fps, const std::string& outdir_raw);
  // depth.mp4 (gray stream)
  bool start_gray(int width, int height, int fps, const std::string& outdir_raw);

  // Synchronously write a segment of raw bytes (called by the background thread)
  bool write(const void* data, size_t bytes);

  void stop();

  bool alive() const { return hProc_ != NULL; }

  HANDLE hWrite() const { return hWrite_; }

private:
  bool start_cmd(const std::string& cmdline, const std::string& outdir_raw);

  HANDLE hRead_ = NULL, hWrite_ = NULL;         // stdin pipe
  HANDLE hErrRead_ = NULL, hErrWrite_ = NULL;   // stderr pipe
  PROCESS_INFORMATION pi_{};                    // 进程信息
  HANDLE hProc_ = NULL;
};
