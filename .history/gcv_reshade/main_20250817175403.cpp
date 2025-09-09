// Copyright (C) 2022 Jason Bunk
#include <imgui.h>
#include "image_writer_thread_pool.h"
#include "generic_depth_struct.h"
#include "gcv_games/game_interface_factory.h"
#include "gcv_utils/miscutils.h"
#include "render_target_stats/render_target_stats_tracking.hpp"
#include "segmentation/reshade_hooks.hpp"
#include "segmentation/segmentation_app_data.hpp"
#include "copy_texture_into_packedbuf.h"
#include "tex_buffer_utils.h"
#include <fstream>
#include <Windows.h>
#include <cstdio>
#include <vector>
#include <memory>
#include <string>
#include <sstream>
#include <cstdint>
#include <thread>
#include <atomic>
#include <cstring> 
#include <process.h>
#include <ShlObj.h>
#include <algorithm>

// --------------------额外工具，到时候挪走
// ======== 轻量 5x7 字体（只含需要的字符）========
static const uint8_t FONT5x7[][7] = {
    // ' ' (32)
    {0x00,0x00,0x00,0x00,0x00,0x00,0x00},
    // 'A'(65)
    {0x1E,0x11,0x11,0x1F,0x11,0x11,0x11},
    // 'C'
    {0x0E,0x11,0x10,0x10,0x10,0x11,0x0E},
    // 'D'
    {0x1E,0x11,0x11,0x11,0x11,0x11,0x1E},
    // 'E'
    {0x1F,0x10,0x10,0x1E,0x10,0x10,0x1F},
    // 'F'
    {0x1F,0x10,0x10,0x1E,0x10,0x10,0x10},
    // 'H'
    {0x11,0x11,0x11,0x1F,0x11,0x11,0x11},
    // 'I'
    {0x1F,0x04,0x04,0x04,0x04,0x04,0x1F},
    // 'P'
    {0x1E,0x11,0x11,0x1E,0x10,0x10,0x10},
    // 'S'
    {0x0F,0x10,0x10,0x0E,0x01,0x01,0x1E},
    // 'T'
    {0x1F,0x04,0x04,0x04,0x04,0x04,0x04},
    // 'W'
    {0x11,0x11,0x15,0x15,0x15,0x1B,0x11},
};
static int glyph_index(char c){
    switch(c){
        case ' ': return 0;
        case 'A': return 1;
        case 'C': return 2;
        case 'D': return 3;
        case 'E': return 4;
        case 'F': return 5;
        case 'H': return 6;
        case 'I': return 7;
        case 'P': return 8;
        case 'S': return 9;
        case 'T': return 10;
        case 'W': return 11;
        default:  return 0; // 空格
    }
}

// ======== 基础：BGRA & Gray 像素混合/矩形 ========
static inline uint8_t u8clamp(int v){ return (uint8_t)(v<0?0:(v>255?255:v)); }

static inline void blend_px_bgra(uint8_t* img,int W,int H,int x,int y,
                                 uint8_t r,uint8_t g,uint8_t b,uint8_t a){
    if(x<0||y<0||x>=W||y>=H) return;
    size_t i=((size_t)y*(size_t)W+(size_t)x)*4;
    float af=a/255.f, ia=1.f-af;
    img[i+0]=u8clamp((int)(af*b + ia*img[i+0]));
    img[i+1]=u8clamp((int)(af*g + ia*img[i+1]));
    img[i+2]=u8clamp((int)(af*r + ia*img[i+2]));
    img[i+3]=255;
}
static inline void blend_px_gray(uint8_t* img,int W,int H,int x,int y,
                                 uint8_t v,uint8_t a){
    if(x<0||y<0||x>=W||y>=H) return;
    size_t i=(size_t)y*(size_t)W+(size_t)x;
    float af=a/255.f, ia=1.f-af;
    img[i]=u8clamp((int)(af*v + ia*img[i]));
}

static void fill_rect_bgra(uint8_t* img,int W,int H,int x0,int y0,int w,int h,
                           uint8_t r,uint8_t g,uint8_t b,uint8_t a=255){
    for(int y=y0;y<y0+h;++y) for(int x=x0;x<x0+w;++x) blend_px_bgra(img,W,H,x,y,r,g,b,a);
}
static void rect_outline_bgra(uint8_t* img,int W,int H,int x0,int y0,int w,int h,
                              uint8_t r,uint8_t g,uint8_t b,int thick=2,uint8_t a=255){
    for(int t=0;t<thick;++t){
        int x1=x0+w-1-t, y1=y0+h-1-t;
        for(int x=x0+t;x<=x1;++x){ blend_px_bgra(img,W,H,x,y0+t,r,g,b,a); blend_px_bgra(img,W,H,x,y1,r,g,b,a);}
        for(int y=y0+t;y<=y1;++y){ blend_px_bgra(img,W,H,x0+t,y,r,g,b,a); blend_px_bgra(img,W,H,x1,y,r,g,b,a);}
    }
}
static void fill_rect_gray(uint8_t* img,int W,int H,int x0,int y0,int w,int h,
                           uint8_t v,uint8_t a=255){
    for(int y=y0;y<y0+h;++y) for(int x=x0;x<x0+w;++x) blend_px_gray(img,W,H,x,y,v,a);
}
static void rect_outline_gray(uint8_t* img,int W,int H,int x0,int y0,int w,int h,
                              uint8_t v,int thick=2,uint8_t a=255){
    for(int t=0;t<thick;++t){
        int x1=x0+w-1-t, y1=y0+h-1-t;
        for(int x=x0+t;x<=x1;++x){ blend_px_gray(img,W,H,x,y0+t,v,a); blend_px_gray(img,W,H,x,y1,v,a); }
        for(int y=y0+t;y<=y1;++y){ blend_px_gray(img,W,H,x0+t,y,v,a); blend_px_gray(img,W,H,x1,y,v,a); }
    }
}

// ======== 写字：BGRA & Gray ========
static void draw_char_bgra(uint8_t* img,int W,int H,int x,int y,char c,int scale,
                           uint8_t r,uint8_t g,uint8_t b,uint8_t a=255){
    const uint8_t* g7 = FONT5x7[glyph_index(c)];
    for(int gy=0; gy<7; ++gy){
        uint8_t row=g7[gy];
        for(int gx=0; gx<5; ++gx){
            if(row & (1<<(4-gx))){
                for(int yy=0; yy<scale; ++yy)
                    for(int xx=0; xx<scale; ++xx)
                        blend_px_bgra(img,W,H,x+gx*scale+xx,y+gy*scale+yy,r,g,b,a);
            }
        }
    }
}
static void draw_text_bgra(uint8_t* img,int W,int H,int x,int y,const char* s,int scale,
                           uint8_t r,uint8_t g,uint8_t b,uint8_t a=255){
    int cx=x; for(const char* p=s; *p; ++p){ draw_char_bgra(img,W,H,cx,y,*p,scale,r,g,b,a); cx += (5*scale + scale); }
}

static void draw_char_gray(uint8_t* img,int W,int H,int x,int y,char c,int scale,
                           uint8_t v,uint8_t a=255){
    const uint8_t* g7 = FONT5x7[glyph_index(c)];
    for(int gy=0; gy<7; ++gy){
        uint8_t row=g7[gy];
        for(int gx=0; gx<5; ++gx){
            if(row & (1<<(4-gx))){
                for(int yy=0; yy<scale; ++yy)
                    for(int xx=0; xx<scale; ++xx)
                        blend_px_gray(img,W,H,x+gx*scale+xx,y+gy*scale+yy,v,a);
            }
        }
    }
}
static void draw_text_gray(uint8_t* img,int W,int H,int x,int y,const char* s,int scale,
                           uint8_t v,uint8_t a=255){
    int cx=x; for(const char* p=s; *p; ++p){ draw_char_gray(img,W,H,cx,y,*p,scale,v,a); cx += (5*scale + scale); }
}
static void ensure_dir_existsA(const std::string& dir) {
    std::string d = dir;
    for (auto& ch : d) if (ch == '/') ch = '\\';
    if (!d.empty() && d.back() != '\\') d.push_back('\\');
    SHCreateDirectoryExA(nullptr, d.c_str(), nullptr);
}
// ----------------------------------------------------------------------------
// -----------------------------------control信号---------------------------
// keymask 位同你现有：W/A/S/D/SHIFT/SPACE 分别是 0..5
#ifndef KM_W
#define KM_W     (1u<<0)
#define KM_A     (1u<<1)
#define KM_S     (1u<<2)
#define KM_D     (1u<<3)
#define KM_SHIFT (1u<<4)
#define KM_SPACE (1u<<5)
#endif

static void draw_keys_hud_bgra(uint8_t* img,int W,int H,uint32_t keymask){
    // 尺寸更大：随分辨率缩放
    const int pad   = std::max(20, W/64);
    const int box   = std::max(64, W/24);   // 大号按键
    const int gap   = std::max(10, W/160);
    const int thick = std::max(3,  W/480);
    const int font  = std::max(3,  box/10); // 字体缩放

    const int hudW = box*3 + gap*2;
    const int hudH = box*2 + gap*2 + box/2 + gap + box/2;

    int ox = pad;
    int oy = H - pad - hudH;

    // 背板（半透明黑）
    fill_rect_bgra(img,W,H, ox-6,oy-6, hudW+12, hudH+12, 0,0,0,140);

    auto key = [&](int x,int y,const char* label,bool on){
        // 按下亮、未按灰
        fill_rect_bgra(img,W,H,x,y,box,box, on? 230:64, on?230:64, on?230:64, 220);
        rect_outline_bgra(img,W,H,x,y,box,box, 255,255,255, thick, 200);
        // 居中写字（略简化）
        int tx = x + (box - 5*font)/2;
        int ty = y + (box - 7*font)/2;
        draw_text_bgra(img,W,H, tx,ty, label, font, 0,0,0, 255);
    };

    // WASD
    int xW = ox + box + gap;   int yW = oy;
    int xA = ox;               int yA = oy + box + gap;
    int xS = ox + box + gap;   int yS = yA;
    int xD = ox + (box+gap)*2; int yD = yA;
    key(xW,yW,"W", (keymask&KM_W)!=0);
    key(xA,yA,"A", (keymask&KM_A)!=0);
    key(xS,yS,"S", (keymask&KM_S)!=0);
    key(xD,yD,"D", (keymask&KM_D)!=0);

    // SPACE / SHIFT 条形键
    auto bar = [&](int x,int y,int w,int h,const char* label,bool on){
        fill_rect_bgra(img,W,H,x,y,w,h, on?230:64, on?230:64, on?230:64, 220);
        rect_outline_bgra(img,W,H,x,y,w,h,255,255,255, thick,200);
        int tx = x + 8;
        int ty = y + (h - 7*font)/2;
        draw_text_bgra(img,W,H, tx,ty, label, font, 0,0,0,255);
    };
    int shY = yA + box + gap, shH = box/2;
    int spY = shY + shH + gap, spH = box/2;
    bar(ox, shY, hudW, shH, "Shift", (keymask&KM_SHIFT)!=0);
    bar(ox, spY, hudW, spH, "Space", (keymask&KM_SPACE)!=0);
}

// ---------------------------------------------------------------

// ===== 灰度帧绘制工具（depth.mp4 用）=====

static void draw_keys_hud_gray(uint8_t* img,int W,int H,uint32_t keymask){
    const int pad   = std::max(20, W/64);
    const int box   = std::max(64, W/24);
    const int gap   = std::max(10, W/160);
    const int thick = std::max(3,  W/480);
    const int font  = std::max(3,  box/10);

    const int hudW = box*3 + gap*2;
    const int hudH = box*2 + gap*2 + box/2 + gap + box/2;

    int ox = pad, oy = H - pad - hudH;

    // 背板
    fill_rect_gray(img,W,H, ox-6,oy-6, hudW+12, hudH+12, 0, 140);

    auto key = [&](int x,int y,const char* label,bool on){
        fill_rect_gray(img,W,H,x,y,box,box, on? 230:64, 220);
        rect_outline_gray(img,W,H,x,y,box,box, 255, thick, 200);
        int tx = x + (box - 5*font)/2;
        int ty = y + (box - 7*font)/2;
        draw_text_gray(img,W,H, tx,ty, label, font, 0,255);
    };

    int xW = ox + box + gap;   int yW = oy;
    int xA = ox;               int yA = oy + box + gap;
    int xS = ox + box + gap;   int yS = yA;
    int xD = ox + (box+gap)*2; int yD = yA;
    key(xW,yW,"W", (keymask&KM_W)!=0);
    key(xA,yA,"A", (keymask&KM_A)!=0);
    key(xS,yS,"S", (keymask&KM_S)!=0);
    key(xD,yD,"D", (keymask&KM_D)!=0);

    auto bar = [&](int x,int y,int w,int h,const char* label,bool on){
        fill_rect_gray(img,W,H,x,y,w,h, on?230:64, 220);
        rect_outline_gray(img,W,H,x,y,w,h,255, thick,200);
        int tx = x + 8;
        int ty = y + (h - 7*font)/2;
        draw_text_gray(img,W,H, tx,ty, label, font, 0,255);
    };
    int shY = yA + box + gap, shH = box/2;
    int spY = shY + shH + gap, spH = box/2;
    bar(ox, shY, hudW, shH, "Shift", (keymask&KM_SHIFT)!=0);
    bar(ox, spY, hudW, spH, "Space", (keymask&KM_SPACE)!=0);
}
// ----------------------------------------------------------------------------
typedef std::chrono::steady_clock hiresclock;
// 录制状态
static bool g_streaming = false;
static int  g_video_fps = 30;
static int  g_fail_in_row = 0;
static const int g_fail_stop_threshold = 20;
static bool g_recording = false;
static std::string g_rec_dir;
static FILE* g_actions_csv = nullptr;
static uint64_t g_rec_idx = 0;
static int64_t g_last_cap_us = 0;
static int g_copy_fail_in_row = 0;
static const int g_copy_fail_stop_threshold = 60;
static float g_depth_clip_low  = 0.0f;  // 归一化下截断（近）
static float g_depth_clip_high = 1.0f;  // 归一化上截断（远）——比如改 0.98f 可抑制天空
static float g_depth_log_alpha = 6.0f;  // 对数增强强度，越大越“拉近景”层次

// 用于补帧的最近一帧缓存
static std::vector<uint8_t> g_last_bgra;
static int g_last_w = 0, g_last_h = 0;

static std::vector<uint8_t> g_last_gray;
static int g_last_dw = 0, g_last_dh = 0;

// 统计
static std::atomic<uint64_t> g_dup_color{0}, g_dup_depth{0};

struct FfmpegPipe {
    HANDLE hRead = NULL, hWrite = NULL;   // stdin pipe
    HANDLE hErrRead = NULL, hErrWrite = NULL; // stderr pipe
    PROCESS_INFORMATION pi{};
    HANDLE hProc = NULL;

    bool start(const std::string &cmdline, const std::string& out_dir_raw) {
		SECURITY_ATTRIBUTES sa{ sizeof(SECURITY_ATTRIBUTES) };
		sa.bInheritHandle = TRUE;
		sa.lpSecurityDescriptor = NULL;

		std::string out_dir = out_dir_raw;
		for (auto &ch : out_dir) if (ch == '/') ch = '\\';
		if (!out_dir.empty() && out_dir.back() != '\\') out_dir.push_back('\\');
		SHCreateDirectoryExA(nullptr, out_dir.c_str(), nullptr);

		// stdin pipe
		if (!CreatePipe(&hRead, &hWrite, &sa, 1 << 20)) return false;
		SetHandleInformation(hWrite, HANDLE_FLAG_INHERIT, 0);

		// stderr pipe
		if (!CreatePipe(&hErrRead, &hErrWrite, &sa, 1 << 15)) {
			CloseHandle(hRead); CloseHandle(hWrite);
			hRead = hWrite = NULL;
			return false;
		}
		SetHandleInformation(hErrRead, HANDLE_FLAG_INHERIT, 0);

		STARTUPINFOA si{};
		si.cb = sizeof(si);
		si.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
		si.wShowWindow = SW_HIDE;
		si.hStdInput  = hRead;
		si.hStdError  = hErrWrite;
		si.hStdOutput = GetStdHandle(STD_OUTPUT_HANDLE);

		ZeroMemory(&pi, sizeof(pi));

		BOOL ok = CreateProcessA(
			NULL,
			(LPSTR)cmdline.c_str(),
			NULL, NULL,
			TRUE, CREATE_NO_WINDOW,
			NULL, NULL,
			&si, &pi
		);
		// 父进程不需要这些端
		CloseHandle(hRead);  hRead  = NULL;
		CloseHandle(hErrWrite); hErrWrite = NULL;

		if (!ok) {
			CloseHandle(hWrite); hWrite = NULL;
			CloseHandle(hErrRead); hErrRead = NULL;
			return false;
		}

		hProc = pi.hProcess;

		// === 新增：短暂等待并检测是否“秒退”，若已退出则清理并返回 false，让上层回退到 x264
		Sleep(120);
		DWORD code = 0;
		if (!GetExitCodeProcess(pi.hProcess, &code) || code != STILL_ACTIVE) {
			// 把 stderr 里可能的错误（例如 nvenc 驱动不匹配）读出来打一条日志
			char buf[512];
			for (;;) {
				DWORD got = 0;
				if (!ReadFile(hErrRead, buf, sizeof(buf)-1, &got, nullptr) || got == 0) break;
				buf[got] = 0;
				reshade::log_message(reshade::log_level::error,
									std::string("[ffmpeg] ").append(buf).c_str());
			}
			// 彻底清理
			if (pi.hThread) { CloseHandle(pi.hThread); pi.hThread = NULL; }
			if (pi.hProcess){ CloseHandle(pi.hProcess); pi.hProcess = NULL; }
			if (hWrite) { CloseHandle(hWrite); hWrite = NULL; }
			if (hErrRead) { CloseHandle(hErrRead); hErrRead = NULL; }
			hProc = NULL;
			return false;
		}
		// === 新增结束 ===

		// 开一个线程持续读 stderr（运行期日志）
		_beginthreadex(nullptr, 0, [](void *arg) -> unsigned {
			FfmpegPipe *self = (FfmpegPipe*)arg;
			char buf[512];
			for (;;) {
				DWORD got = 0;
				if (!ReadFile(self->hErrRead, buf, sizeof(buf) - 1, &got, nullptr) || got == 0)
					break;
				buf[got] = 0;
				reshade::log_message(reshade::log_level::error,
									std::string("[ffmpeg] ").append(buf).c_str());
			}
			return 0u;
		}, this, 0, nullptr);

		return true;
	}

	bool start(int width, int height, int fps, const std::string& outdir_raw) {
		// 规范输出目录
		std::string outdir = outdir_raw;
		for (auto &ch : outdir) if (ch == '/') ch = '\\';
		if (!outdir.empty() && outdir.back() != '\\') outdir.push_back('\\');
		const std::string out_mp4 = outdir + "capture.mp4";
		// 回退 x264
		std::ostringstream cmd_x264;
		cmd_x264 << "ffmpeg -loglevel error -y "
				<< "-re "                           // 按实时速度读取管道
				<< "-f rawvideo -pix_fmt bgra "
				<< "-s " << width << "x" << height << " "
				<< "-framerate " << fps << " "      // 输入帧率（原始帧）
				<< "-i pipe:0 "
				<< "-vsync cfr -r " << fps << " "   // 强制固定帧率输出
				<< "-c:v libx264 -preset veryfast -crf 18 "
				<< "-pix_fmt yuv420p -movflags +faststart "
				<< "\"" << out_mp4 << "\"";
		return start(cmd_x264.str(), outdir);
	}
	bool start_depth(int width, int height, int fps, const std::string& outdir_raw) {
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
		return start(cmd.str(), outdir);
	}
    void stop() {
        if (hWrite) { CloseHandle(hWrite); hWrite = NULL; }
        if (hErrRead) { CloseHandle(hErrRead); hErrRead = NULL; }
        if (hErrWrite){ CloseHandle(hErrWrite); hErrWrite = NULL; }

        if (pi.hThread) { CloseHandle(pi.hThread); pi.hThread = NULL; }
        if (pi.hProcess){ CloseHandle(pi.hProcess); pi.hProcess = NULL; }
        hProc = NULL;
    }
};

static FfmpegPipe g_ff;
static FfmpegPipe g_ff_depth;
static std::atomic<uint64_t> g_enqueued{0}, g_written{0};
struct RawFrame {
    std::unique_ptr<uint8_t[]> data;
    size_t size = 0;
    int w = 0, h = 0;
    size_t stride = 0;
};

static std::vector<RawFrame> g_ring(8);   // 8 帧缓冲
static std::atomic<uint32_t> g_prod{0}, g_cons{0};
static std::atomic<bool> g_pipe_thread_run{false};
static std::thread g_pipe_thread;
static bool ring_push(RawFrame&& f) {
    uint32_t p = g_prod.load(std::memory_order_relaxed);
    uint32_t c = g_cons.load(std::memory_order_acquire);
    if ((p - c) >= g_ring.size()) return false;          // 满
    g_ring[p % g_ring.size()] = std::move(f);
    g_prod.store(p+1, std::memory_order_release);
	g_enqueued.fetch_add(1, std::memory_order_relaxed);
    return true;
}
static bool ring_pop(RawFrame& out) {
    uint32_t c = g_cons.load(std::memory_order_relaxed);
    uint32_t p = g_prod.load(std::memory_order_acquire);
    if (c == p) return false;                            // 空
    out = std::move(g_ring[c % g_ring.size()]);
    g_cons.store(c+1, std::memory_order_release);
    return true;
}

static void pipe_writer_loop() {
    RawFrame f;
    while (g_pipe_thread_run.load(std::memory_order_acquire)) {
        if (!ring_pop(f)) { Sleep(1); continue; }
        if (f.data && f.size && g_ff.hProc && g_ff.hWrite) {
            DWORD wrote = 0;
            if (!WriteFile(g_ff.hWrite, f.data.get(), (DWORD)f.size, &wrote, nullptr)) {
                DWORD e = GetLastError();
                char s[128];
                _snprintf_s(s, _TRUNCATE, "[CV Capture] WriteFile to ffmpeg failed, err=%lu", (unsigned long)e);
                reshade::log_message(reshade::log_level::error, s);
                // 直接停：子进程大概率已经退出/管道断开
                g_pipe_thread_run.store(false, std::memory_order_release);
                break;
            }else {
				g_written.fetch_add(1, std::memory_order_relaxed); // 新增
			}
        }
        f = {};
    }
}

// depth
struct RawFrameGray {
    std::unique_ptr<uint8_t[]> data;
    size_t size = 0;
    int w = 0, h = 0;
    size_t stride = 0; // = w * 1
};

static std::vector<RawFrameGray> g_ring_d(8);
static std::atomic<uint32_t> g_prod_d{0}, g_cons_d{0};
static std::atomic<bool> g_pipe_thread_run_d{false};
static std::thread g_pipe_thread_d;

static bool ring_push_d(RawFrameGray&& f) {
    uint32_t p = g_prod_d.load(std::memory_order_relaxed);
    uint32_t c = g_cons_d.load(std::memory_order_acquire);
    if ((p - c) >= g_ring_d.size()) return false;
    g_ring_d[p % g_ring_d.size()] = std::move(f);
    g_prod_d.store(p+1, std::memory_order_release);
    return true;
}
static bool ring_pop_d(RawFrameGray& out) {
    uint32_t c = g_cons_d.load(std::memory_order_relaxed);
    uint32_t p = g_prod_d.load(std::memory_order_acquire);
    if (c == p) return false;
    out = std::move(g_ring_d[c % g_ring_d.size()]);
    g_cons_d.store(c+1, std::memory_order_release);
    return true;
}

static void pipe_writer_loop_d() {
    RawFrameGray f;
    while (g_pipe_thread_run_d.load(std::memory_order_acquire)) {
        if (!ring_pop_d(f)) { Sleep(1); continue; }
        if (f.data && f.size && g_ff_depth.hProc && g_ff_depth.hWrite) {
            DWORD wrote = 0;
            if (!WriteFile(g_ff_depth.hWrite, f.data.get(), (DWORD)f.size, &wrote, nullptr)) {
                DWORD e = GetLastError();
                char s[128];
                _snprintf_s(s, _TRUNCATE, "[CV Capture] depth WriteFile failed, err=%lu", (unsigned long)e);
                reshade::log_message(reshade::log_level::error, s);
                g_pipe_thread_run_d.store(false, std::memory_order_release);
                break;
            }
        }
        f = {};
    }
}

static bool check_alive_and_log_then_fallback(PROCESS_INFORMATION& pi,
                                              HANDLE hWrite,
                                              bool tried_nvenc,
                                              std::string& cmdline_fallback)
{
    // 等 50ms 看看是否秒退
    Sleep(50);
    DWORD code = 0;
    if (!GetExitCodeProcess(pi.hProcess, &code)) {
        reshade::log_message(reshade::log_level::error, "[CV Capture] GetExitCodeProcess failed");
        return false;
    }
    if (code == STILL_ACTIVE) return true; // 正常运行中

    // 已退出：先关句柄
    CloseHandle(pi.hThread); CloseHandle(pi.hProcess);
    CloseHandle(hWrite);

    if (tried_nvenc && !cmdline_fallback.empty()) {
        reshade::log_message(reshade::log_level::warning, "[CV Capture] ffmpeg (nvenc) exited immediately; try x264 fallback");
        return false; // 让上层走 x264 的 CreateProcessA 分支
    }
    reshade::log_message(reshade::log_level::error, "[CV Capture] ffmpeg exited immediately");
    return false;
}

static bool grab_bgra_frame(reshade::api::command_queue* q, reshade::api::resource tex,
                            std::vector<uint8_t>& out_bgra, int& w, int& h) {
    simple_packed_buf pbuf;
    depth_tex_settings depth_cfg{}; // 仅抓 RGB
    if (!copy_texture_image_needing_resource_barrier_into_packedbuf(
            nullptr, pbuf, q, tex, TexInterp_RGB, depth_cfg)) {
        return false;
    }
    w = (int)pbuf.width; h = (int)pbuf.height;
    const size_t row_bgra = (size_t)w * 4;
    out_bgra.resize((size_t)h * row_bgra);

    if (pbuf.pixfmt == BUF_PIX_FMT_RGBA) {
        // RGBA -> BGRA
        for (int y = 0; y < h; ++y) {
            const uint8_t* src = pbuf.rowptr<uint8_t>(y);
            uint8_t* dst = out_bgra.data() + (size_t)y * row_bgra;
            for (int x = 0; x < w; ++x) {
                const uint8_t r = src[4*x+0], g = src[4*x+1], b = src[4*x+2], a = src[4*x+3];
                dst[4*x+0] = b; dst[4*x+1] = g; dst[4*x+2] = r; dst[4*x+3] = a;
            }
        }
        return true;
    } else if (pbuf.pixfmt == BUF_PIX_FMT_RGB24) {
        // RGB -> BGRA (A=255)
        for (int y = 0; y < h; ++y) {
            const uint8_t* src = pbuf.rowptr<uint8_t>(y);
            uint8_t* dst = out_bgra.data() + (size_t)y * row_bgra;
            for (int x = 0; x < w; ++x) {
                const uint8_t r = src[3*x+0], g = src[3*x+1], b = src[3*x+2];
                dst[4*x+0] = b; dst[4*x+1] = g; dst[4*x+2] = r; dst[4*x+3] = 255;
            }
        }
        return true;
    } else {
        reshade::log_message(reshade::log_level::error, "grab_bgra_frame: unsupported pixfmt");
        return false;
    }
}
static bool grab_depth_gray8(reshade::api::command_queue* q,
                             reshade::api::resource depth_tex,
                             std::vector<uint8_t>& out_gray,
                             int& w, int& h)
{
    simple_packed_buf pbuf;
    depth_tex_settings depth_cfg{}; // 先用默认；如需 alreadyfloat / endian，可在这里配置
    if (!copy_texture_image_needing_resource_barrier_into_packedbuf(
            nullptr, pbuf, q, depth_tex, TexInterp_Depth, depth_cfg)) {
        return false;
    }

    w = (int)pbuf.width;
    h = (int)pbuf.height;
    if (w <= 0 || h <= 0) return false;
    out_gray.resize((size_t)w * (size_t)h);

    // 对数映射预计算
    const float alpha   = (g_depth_log_alpha > 0.f ? g_depth_log_alpha : 1.f);
    const float denom   = std::log1p(alpha);
    auto map01_farwhite_log = [&](float t01)->uint8_t {
        // t01：0=近, 1=远  ->  远白近黑
        if (t01 < 0.f) t01 = 0.f; else if (t01 > 1.f) t01 = 1.f;
        float y = std::log1p(alpha * t01) / denom;   // 对数增强
        int g = (int)std::lround(y * 255.0f);
        if (g < 0) g = 0; else if (g > 255) g = 255;
        return (uint8_t)g;
    };

    switch (pbuf.pixfmt) {
    case BUF_PIX_FMT_GRAYF32:
    {
        // 假设 0=近, 1=远；可用 clip 裁掉极端值（天空/无穷远）
        const float lo = g_depth_clip_low;
        const float hi = (g_depth_clip_high > lo ? g_depth_clip_high : lo + 1e-6f);
        const float invspan = 1.0f / (hi - lo);

        for (int y = 0; y < h; ++y) {
            const float* src = pbuf.rowptr<float>(y);
            uint8_t* dst = out_gray.data() + (size_t)y * (size_t)w;
            for (int x = 0; x < w; ++x) {
                float d = src[x];
                if (!std::isfinite(d)) d = hi;           // NaN/Inf 视作“最远”
                float t = (d - lo) * invspan;            // 归一化到 0..1（近->0，远->1）
                // 远白近黑 + 对数增强
                dst[x] = map01_farwhite_log(t);
            }
        }
        return true;
    }
    case BUF_PIX_FMT_GRAYU32:
    {
        // 先按帧内 min-max 归一化到 0..1，再做 clip + log
        uint32_t vmin = UINT32_MAX, vmax = 0;
        for (int y = 0; y < h; ++y) {
            const uint32_t* src = pbuf.rowptr<uint32_t>(y);
            for (int x = 0; x < w; ++x) {
                uint32_t v = src[x];
                if (v < vmin) vmin = v;
                if (v > vmax) vmax = v;
            }
        }
        const double span = (vmax > vmin) ? double(vmax - vmin) : 1.0;

        // 将 min-max 归一化结果再做 clip_low/high（按 0..1 空间）
        const float lo = g_depth_clip_low;
        const float hi = (g_depth_clip_high > lo ? g_depth_clip_high : lo + 1e-6f);
        const float invspan = 1.0f / (hi - lo);

        for (int y = 0; y < h; ++y) {
            const uint32_t* src = pbuf.rowptr<uint32_t>(y);
            uint8_t* dst = out_gray.data() + (size_t)y * (size_t)w;
            for (int x = 0; x < w; ++x) {
                float t0 = float((double(src[x]) - double(vmin)) / span); // 0..1
                float t  = (t0 - lo) * invspan;
                if (t < 0.f) t = 0.f; else if (t > 1.f) t = 1.f;
                dst[x] = map01_farwhite_log(t);
            }
        }
        return true;
    }
    default:
        reshade::log_message(reshade::log_level::error, "grab_depth_gray8: unsupported depth pixfmt");
        return false;
    }
}

static void on_init(reshade::api::device* device)
{
	auto &shdata = device->create_private_data<image_writer_thread_pool>();
	reshade::log_message(reshade::log_level::info, std::string(std::string("tests: ")+run_utils_tests()).c_str());
	shdata.init_time = hiresclock::now();
}
// 读回：把 GPU 纹理复制到 CPU，并得到 BGRA 指针与 stride（行距）
// 注意：为简化使用，临时资源每帧创建/销毁；如需极致性能可做缓存
static bool copy_color_to_cpu_bgra(reshade::api::device* device, reshade::api::command_queue* queue,
    reshade::api::resource src_tex, std::unique_ptr<uint8_t[]>& out_ptr, size_t& out_stride, int& w, int& h)
{
    if (src_tex.handle == 0) return false;
    const auto srcd = device->get_resource_desc(src_tex);
    if (srcd.type != reshade::api::resource_type::texture_2d) return false;
    w = (int)srcd.texture.width;
    h = (int)srcd.texture.height;

    // 构造“干净”的 staging 描述，避免继承源纹理不兼容标志
    reshade::api::resource_desc staging_desc = {};
    staging_desc.type = reshade::api::resource_type::texture_2d;
    staging_desc.heap = reshade::api::memory_heap::gpu_to_cpu;
    staging_desc.texture.width = srcd.texture.width;
    staging_desc.texture.height = srcd.texture.height;
    staging_desc.texture.depth_or_layers = 1;
    staging_desc.texture.levels = 1;
    staging_desc.texture.format = srcd.texture.format; // 28:r8g8b8a8_unorm
    staging_desc.texture.samples = 1;

    reshade::api::resource staging = {};
    if (!device->create_resource(staging_desc, nullptr, reshade::api::resource_usage::copy_dest, &staging)) {
        reshade::log_message(reshade::log_level::warning, "copy_color_to_cpu_bgra: create_resource failed");
        return false;
    }

    reshade::api::command_list* cmdlist = queue->get_immediate_command_list();

    // 渲染目标 -> 拷贝源
    cmdlist->barrier(src_tex, reshade::api::resource_usage::render_target, reshade::api::resource_usage::copy_source);
    reshade::api::subresource_box box{ 0,0,0, (int)srcd.texture.width, (int)srcd.texture.height, 1 };
    cmdlist->copy_texture_region(src_tex, 0, nullptr, staging, 0, &box);
    cmdlist->barrier(src_tex, reshade::api::resource_usage::copy_source, reshade::api::resource_usage::render_target);
    queue->flush_immediate_command_list();

    // 映射
    reshade::api::subresource_data mapped{};
    if (!device->map_texture_region(staging, 0, nullptr, reshade::api::map_access::read_only, &mapped)) {
        device->destroy_resource(staging);
        reshade::log_message(reshade::log_level::warning, "copy_color_to_cpu_bgra: map_texture_region failed");
        return false;
    }
    out_stride = mapped.row_pitch;
    const size_t bytes = out_stride * h;
    out_ptr.reset(new uint8_t[bytes]);
    memcpy(out_ptr.get(), mapped.data, bytes);
    device->unmap_texture_region(staging, 0);
    device->destroy_resource(staging);
    return true;
}
static void on_destroy(reshade::api::device* device)
{
    device->get_private_data<image_writer_thread_pool>().change_num_threads(0);
    device->get_private_data<image_writer_thread_pool>().print_waiting_log_messages();

    // 追加：停止推流与关闭 CSV
    if (g_streaming) { g_streaming = false; g_ff.stop(); }
    if (g_actions_csv) { fclose(g_actions_csv); g_actions_csv = nullptr; }
	if (g_pipe_thread_run.exchange(false)) {
		if (g_pipe_thread.joinable()) g_pipe_thread.join();
		reshade::log_message(reshade::log_level::info, "[CV Capture] pipe thread joined");
	}
	g_ff.stop();
	if (g_pipe_thread_run_d.exchange(false)) {
		if (g_pipe_thread_d.joinable()) g_pipe_thread_d.join();
		reshade::log_message(reshade::log_level::info, "[CV Capture] depth pipe thread joined");
	}
	g_ff_depth.stop();
	char s[128];
	_snprintf_s(s, _TRUNCATE, "[CV Capture] frames enqueued=%llu, written=%llu",
            (unsigned long long)g_enqueued.load(), (unsigned long long)g_written.load());
	reshade::log_message(reshade::log_level::info, s);

    device->destroy_private_data<image_writer_thread_pool>();
}

static void on_reshade_finish_effects(reshade::api::effect_runtime *runtime,
	reshade::api::command_list *, reshade::api::resource_view rtv, reshade::api::resource_view)
{
	auto &shdata = runtime->get_device()->get_private_data<image_writer_thread_pool>();
	CamMatrixData gamecam;
	std::string errstr;
	bool shaderupdatedwithcampos = false;
	float shadercamposbuf[4];
	reshade::api::device* const device = runtime->get_device();
	auto& segmapp = device->get_private_data<segmentation_app_data>();
	{
        const int64_t now_us = std::chrono::duration_cast<std::chrono::microseconds>(hiresclock::now() - shdata.init_time).count();
        const bool ctrl_down = (GetAsyncKeyState(VK_CONTROL) & 0x8000) != 0;
		if (ctrl_down && runtime->is_key_pressed(VK_F9) && !g_recording) {
			const std::string dirname = std::string("actions_") + get_datestr_yyyy_mm_dd() + "_" +
										std::to_string(now_us) + "/";
			g_rec_dir = shdata.output_filepath_creates_outdir_if_needed(dirname);

			// 确保目录真的存在（有些实现只返回路径字符串，不一定真的创建）
			ensure_dir_existsA(g_rec_dir);

			const std::string csvpth = g_rec_dir + "actions.csv";

			// 用 _fsopen + 无缓冲，避免崩溃时丢数据
			g_actions_csv = _fsopen(csvpth.c_str(), "w", _SH_DENYNO);
			if (!g_actions_csv) {
				char buf[256];
				_snprintf_s(buf, _TRUNCATE, "[CV Capture] fopen failed for %s (errno=%d)", csvpth.c_str(), errno);
				reshade::log_message(reshade::log_level::error, buf);
			} else {
				// 取消缓冲：每次 fprintf 都直接落盘
				setvbuf(g_actions_csv, nullptr, _IONBF, 0);
				fprintf(g_actions_csv, "frame_idx,time_us,keymask\n");
			}

			g_recording = true;
			g_streaming = true;
			g_rec_idx = 0;
			g_last_cap_us = 0;
			g_fail_in_row = 0;
			g_copy_fail_in_row = 0;
			g_prod = 0; g_cons = 0; // 清 ring 指针

			reshade::log_message(reshade::log_level::info, ("REC start: " + g_rec_dir).c_str());
		}

		if (ctrl_down && runtime->is_key_pressed(VK_F10) && g_recording) {
			g_recording = false;
			g_streaming = false;

			// 先停写线程，再停 ffmpeg
			if (g_pipe_thread_run.exchange(false)) {
				if (g_pipe_thread.joinable()) g_pipe_thread.join();
			}

			if (g_pipe_thread_run_d.exchange(false)) {
				if (g_pipe_thread_d.joinable()) g_pipe_thread_d.join();
			}
			g_ff.stop();
			g_ff_depth.stop();
			if (g_actions_csv) { fclose(g_actions_csv); g_actions_csv = nullptr; }
			reshade::log_message(reshade::log_level::info, "REC stop");
		}

        if (g_recording && g_streaming) {
			const int fps = std::max(1, g_video_fps);
			const int64_t period_us = 1000000LL / fps;

			static int64_t next_due_us = 0;            // 下一帧应当写入的时间点
			if (g_last_cap_us == 0) {
				g_last_cap_us = now_us;
				next_due_us   = now_us;                // 从当前时刻开始对齐
			}

			if (now_us >= next_due_us) {
				// 如果落后了，算需要写多少帧（含补帧）
				int64_t behind = now_us - next_due_us;
				int need = 1 + (int)(behind / period_us);       // 需要写入的帧数
				if (need > 8) need = 8; 

				// 键盘状态
				uint32_t keymask = 0;
				if (GetAsyncKeyState('W') & 0x8000) keymask |= 1u << 0;
				if (GetAsyncKeyState('A') & 0x8000) keymask |= 1u << 1;
				if (GetAsyncKeyState('S') & 0x8000) keymask |= 1u << 2;
				if (GetAsyncKeyState('D') & 0x8000) keymask |= 1u << 3;
				if (GetAsyncKeyState(VK_SHIFT) & 0x8000) keymask |= 1u << 4;
				if (GetAsyncKeyState(VK_SPACE) & 0x8000) keymask |= 1u << 5;

				// GPU->CPU 读回 BGRA
				reshade::api::device* const dev = runtime->get_device();
				reshade::api::command_queue* const q = runtime->get_command_queue();
				const reshade::api::resource color_res = dev->get_resource_from_view(rtv);

				if (color_res.handle == 0) {
					reshade::log_message(reshade::log_level::warning, "stream skip: color resource null");
				} else {
					std::vector<uint8_t> bgra;
					int w = 0, h = 0;
					if (grab_bgra_frame(q, color_res, bgra, w, h)) {
						g_copy_fail_in_row = 0;
						draw_keys_hud_bgra(bgra.data(), w, h, keymask);
						g_last_bgra = bgra; g_last_w = w; g_last_h = h;
						// 首帧：拉起 ffmpeg 与写线程
						if (!g_ff.hProc) {
							if (!g_ff.start(w, h, g_video_fps, g_rec_dir)) {
								reshade::log_message(reshade::log_level::error, "ffmpeg start failed; stop streaming");
								g_streaming = false;
							} else if (!g_pipe_thread_run.load()) {
								g_pipe_thread_run = true;
								g_pipe_thread = std::thread(pipe_writer_loop);
							}
						}

						if (g_ff.hProc) {
							RawFrame f;
							f.w = w; f.h = h; f.stride = (size_t)w * 4;
							f.size = f.stride * (size_t)h;
							f.data.reset(new uint8_t[f.size]);
							// 三个参数的 memcpy（需要 <cstring>）
							std::memcpy(f.data.get(), bgra.data(), f.size);

							if (!ring_push(std::move(f))) {
								// 队列满：丢帧
							} else {
								if (g_actions_csv) {
									auto b = [&](unsigned bit){ return (keymask & (1u<<bit)) ? 1 : 0; };
									std::fprintf(g_actions_csv, "%llu,%lld,%d,%d,%d,%d,%d,%d\n",
										(unsigned long long)g_rec_idx, (long long)now_us,
										b(0), b(1), b(2), b(3), b(4), b(5)); // W A S D SHIFT SPACE
								}
								g_rec_idx++;
							}
						}
					} else {
						g_copy_fail_in_row++;
						reshade::log_message(reshade::log_level::warning, "stream copy to CPU failed");
						if (g_copy_fail_in_row >= g_copy_fail_stop_threshold) {
							reshade::log_message(reshade::log_level::error, "copy failed too many times; stop streaming");
							g_streaming = false;

							if (g_pipe_thread_run.exchange(false)) {
								if (g_pipe_thread.joinable()) g_pipe_thread.join();
							}
							g_ff.stop();
						}
					}
					
				}
				generic_depth_data &genericdepdata = runtime->get_private_data<generic_depth_data>();
				reshade::api::resource depth_res = genericdepdata.selected_depth_stencil;

				if (depth_res.handle != 0) {
					std::vector<uint8_t> gray;
					int dw = 0, dh = 0;
					if (grab_depth_gray8(q, depth_res, gray, dw, dh)) {
						draw_keys_hud_gray(gray.data(), dw, dh, keymask);
						g_last_gray = gray; g_last_dw = dw; g_last_dh = dh;
						// 首帧：拉起深度 ffmpeg 与写线程
						if (!g_ff_depth.hProc) {
							if (!g_ff_depth.start_depth(dw, dh, g_video_fps, g_rec_dir)) {
								reshade::log_message(reshade::log_level::error, "ffmpeg start (depth) failed");
							} else if (!g_pipe_thread_run_d.load()) {
								g_pipe_thread_run_d = true;
								g_pipe_thread_d = std::thread(pipe_writer_loop_d);
							}
						}
						if (g_ff_depth.hProc) {
							RawFrameGray df;
							df.w = dw; df.h = dh; df.stride = (size_t)dw;        // gray: 1 byte / pixel
							df.size = df.stride * (size_t)dh;
							df.data.reset(new uint8_t[df.size]);
							std::memcpy(df.data.get(), gray.data(), df.size);
							ring_push_d(std::move(df)); // 队列满了就丢帧，跟 RGB 一样
						}
					} else {
						// 抓深度失败时可按需打日志（避免刷屏）
						// reshade::log_message(reshade::log_level::warning, "stream depth copy failed");
					}
				}
				    for (int i = 0; i < need; ++i) {
						// --- 彩色：如果不是最后一张，就用上一帧做 duplicate ---
						if (g_ff.hProc && g_last_w > 0 && g_last_h > 0) {
							RawFrame f;
							f.w = g_last_w; f.h = g_last_h; f.stride = (size_t)f.w * 4;
							f.size = f.stride * (size_t)f.h;
							f.data.reset(new uint8_t[f.size]);
							std::memcpy(f.data.get(), g_last_bgra.data(), f.size);
							if (!ring_push(std::move(f))) {/* 队列满可忽略 */}
							else if (i < need - 1) g_dup_color.fetch_add(1, std::memory_order_relaxed);
						}
						// --- 深度同理 ---
						if (g_ff_depth.hProc && g_last_dw > 0 && g_last_dh > 0) {
							RawFrameGray df;
							df.w = g_last_dw; df.h = g_last_dh; df.stride = (size_t)df.w;
							df.size = df.stride * (size_t)df.h;
							df.data.reset(new uint8_t[df.size]);
							std::memcpy(df.data.get(), g_last_gray.data(), df.size);
							if (!ring_push_d(std::move(df))) {/* ignore */}
							else if (i < need - 1) g_dup_depth.fetch_add(1, std::memory_order_relaxed);
						}
						next_due_us += period_us;           // 前进到下一个节拍
					}

				g_last_cap_us = now_us;
			}
			return; // 录制模式：跳过下方“按F11拍单帧”的逻辑
		}
    }
	// returns true if frame capture requested
	if (segmentation_app_update_on_finish_effects(runtime, runtime->is_key_pressed(VK_F11)))
	{
		generic_depth_data &genericdepdata = runtime->get_private_data<generic_depth_data>();
		reshade::api::command_queue *cmdqueue = runtime->get_command_queue();
		const int64_t microseconds_elapsed = std::chrono::duration_cast<std::chrono::microseconds>(hiresclock::now() - shdata.init_time).count();
		const std::string microelapsedstr = std::to_string(microseconds_elapsed);
		const std::string basefilen = shdata.gamename_simpler() + std::string("_")
			+ get_datestr_yyyy_mm_dd() + std::string("_") + microelapsedstr + std::string("_");
		std::stringstream capmessage;
		capmessage << "capture " << basefilen << ": ";
		bool capgood = true;
		nlohmann::json metajson;

#if RENDERDOC_FOR_SHADERS
		if(shdata.depth_settings.more_verbose || shdata.depth_settings.debug_mode) {
			if (shdata.save_texture_image_needing_resource_barrier_copy(basefilen + std::string("semsegrawbuffer"),
					ImageWriter_STB_png, cmdqueue, segmapp.r_accum_bonus.rsc, TexInterp_IndexedSeg)) {
				capmessage << "semsegrawbuffer good; ";
			} else {
				capmessage << "semsegrawbuffer failed; ";
				capgood = false;
			}
		}

		if (shdata.save_segmentation_app_indexed_image_needing_resource_barrier_copy(
					basefilen, cmdqueue, metajson)) {
			capmessage << "semseg good; ";
		} else {
			capmessage << "semseg failed; ";
			capgood = false;
		}
#endif

		if (shdata.get_camera_matrix(gamecam, errstr)) {
			gamecam.into_json(metajson);
			metajson["time_us"] = microelapsedstr;
		} else {
			capmessage << "camjson: failed to get any camera data";
			capgood = false;
		}
		if (!errstr.empty()) {
			capmessage << ", " << errstr;
			errstr.clear();
		}
		capmessage << "; ";

		if (!metajson.empty()) {
			std::ofstream outjson(shdata.output_filepath_creates_outdir_if_needed(basefilen + std::string("meta.json")));
			if (outjson.is_open() && outjson.good()) {
				outjson << metajson.dump() << std::endl;
				outjson.close();
				capmessage << "metajson: good; ";
			} else {
				capmessage << "metajson: failed to write; ";
				capgood = false;
			}
		}
		if (!g_streaming) {
			if (shdata.save_texture_image_needing_resource_barrier_copy(basefilen + std::string("RGB"),
				ImageWriter_STB_png, cmdqueue, device->get_resource_from_view(rtv), TexInterp_RGB))
			{
				if (shdata.save_texture_image_needing_resource_barrier_copy(basefilen + std::string("depth"),
					ImageWriter_STB_png | ImageWriter_numpy | (shdata.game_knows_depthbuffer() ? ImageWriter_fpzip : 0),
					cmdqueue, genericdepdata.selected_depth_stencil, TexInterp_Depth))
				{
					capmessage << "RGB and depth good";
				} else {
					capmessage << "RGB good, but failed to capture depth";
					capgood = false;
				}
			} else {
				capmessage << "failed to capture RGB (so didnt try depth)";
				capgood = false;
			}
			if (!errstr.empty()) {
				capmessage << ", " << errstr;
				errstr.clear();
			}
			reshade::log_message(capgood ? reshade::log_level::info : reshade::log_level::error, capmessage.str().c_str());
		}
	}
	if(shdata.grabcamcoords) {
		if (gamecam.extrinsic_status == CamMatrix_Uninitialized) {
			shdata.get_camera_matrix(gamecam, errstr);
		}
		if (gamecam.extrinsic_status != CamMatrix_Uninitialized) {
			shadercamposbuf[3] = 1.0f;
			if (gamecam.extrinsic_status & CamMatrix_PositionGood || gamecam.extrinsic_status & CamMatrix_WIP) {
				for(int ii=0; ii<3; ++ii) shadercamposbuf[ii] = gamecam.extrinsic_cam2world(ii, cam_matrix_position_column);
				runtime->set_uniform_value_float(runtime->find_uniform_variable("displaycamcoords.fx", "dispcam_latestcampos"), shadercamposbuf, 4);
				shaderupdatedwithcampos = true;
			}
			if (gamecam.extrinsic_status & CamMatrix_RotationGood || gamecam.extrinsic_status & CamMatrix_WIP) {
				for (int colidx = 0; colidx < 3; ++colidx) {
					for (int ii = 0; ii < 3; ++ii) shadercamposbuf[ii] = gamecam.extrinsic_cam2world(ii, colidx);
					runtime->set_uniform_value_float(runtime->find_uniform_variable("displaycamcoords.fx", (std::string("dispcam_latestcamcol")+std::to_string(colidx)).c_str()), shadercamposbuf, 4);
				}
				shaderupdatedwithcampos = true;
			}
		}
	}
	if (!shdata.camcoordsinitialized) {
		if (!shaderupdatedwithcampos) {
			shadercamposbuf[0] = 0.0f;
			shadercamposbuf[1] = 0.0f;
			shadercamposbuf[2] = 0.0f;
			shadercamposbuf[3] = 0.0f;
			runtime->set_uniform_value_float(runtime->find_uniform_variable("displaycamcoords.fx", "dispcam_latestcampos"), shadercamposbuf, 4);
		}
		shdata.camcoordsinitialized = true;
	}
	shdata.print_waiting_log_messages();
	segmapp.r_counter_buf.reset_at_end_of_frame();
}

static void draw_settings_overlay(reshade::api::effect_runtime *runtime)
{
	auto &shdata = runtime->get_device()->get_private_data<image_writer_thread_pool>();
	ImGui::Checkbox("Depth map: verbose mode", &shdata.depth_settings.more_verbose);
	if (shdata.depth_settings.more_verbose) {
		ImGui::Checkbox("Depth map: debug mode", &shdata.depth_settings.debug_mode);
		ImGui::Checkbox("Depth map: already float?", &shdata.depth_settings.alreadyfloat);
		ImGui::Checkbox("Depth map: float endian flip?", &shdata.depth_settings.float_reverse_endian);
		ImGui::SliderInt("Depth map: row pitch rescale (powers of 2)", &shdata.depth_settings.adjustpitchhack, -8, 8);
		ImGui::SliderInt("Depth map: bytes per pix", &shdata.depth_settings.depthbytes, 0, 8);
		ImGui::SliderInt("Depth map: bytes per pix to keep", &shdata.depth_settings.depthbyteskeep, 0, 8);
	}
	ImGui::Checkbox("Grab camera coordinates every frame?", &shdata.grabcamcoords);
	if (shdata.grabcamcoords) {
		CamMatrixData lcam; std::string errstr;
		if (shdata.get_camera_matrix(lcam, errstr) != CamMatrix_Uninitialized) {
			ImGui::Text("%f, %f, %f",
				lcam.extrinsic_cam2world(0, cam_matrix_position_column),
				lcam.extrinsic_cam2world(1, cam_matrix_position_column),
				lcam.extrinsic_cam2world(2, cam_matrix_position_column));
		} else {
			ImGui::Text(errstr.c_str());
		}
	}
	ImGui::Text("Render targets:");
	imgui_draw_rgb_render_target_stats_in_reshade_overlay(runtime);
	imgui_draw_custom_shader_debug_viz_in_reshade_overlay(runtime);
}

extern "C" __declspec(dllexport) const char *NAME = "CV Capture";
extern "C" __declspec(dllexport) const char *DESCRIPTION =
    "Add-on that captures the screen after effects were rendered, and also the depth buffer, every time key is pressed.";

BOOL WINAPI DllMain(HINSTANCE hinstDLL, DWORD fdwReason, LPVOID)
{
	switch (fdwReason)
	{
	case DLL_PROCESS_ATTACH:
		if (!reshade::register_addon(hinstDLL))
			return FALSE;
		register_rgb_render_target_stats_tracking();
		register_segmentation_app_hooks();
		reshade::register_event<reshade::addon_event::init_device>(on_init);
		reshade::register_event<reshade::addon_event::destroy_device>(on_destroy);
		reshade::register_event<reshade::addon_event::reshade_finish_effects>(on_reshade_finish_effects);
		reshade::register_overlay(nullptr, draw_settings_overlay);
		break;
	case DLL_PROCESS_DETACH:
		reshade::unregister_event<reshade::addon_event::init_device>(on_init);
		reshade::unregister_event<reshade::addon_event::destroy_device>(on_destroy);
		reshade::unregister_event<reshade::addon_event::reshade_finish_effects>(on_reshade_finish_effects);
		reshade::unregister_overlay(nullptr, draw_settings_overlay);
		unregister_segmentation_app_hooks();
		unregister_rgb_render_target_stats_tracking();
		reshade::unregister_addon(hinstDLL);
		break;
	}
	return TRUE;
}
