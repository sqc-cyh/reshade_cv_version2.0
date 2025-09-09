// Copyright (C) 2022 Jason Bunk
#include <imgui.h>
#include "image_writer_thread_pool.h"
#include "generic_depth_struct.h"
#include "gcv_games/game_interface_factory.h"
#include "gcv_utils/miscutils.h"
#include "render_target_stats/render_target_stats_tracking.hpp"
#include "segmentation/reshade_hooks.hpp"
#include "segmentation/segmentation_app_data.hpp"
#include <fstream>
#include <Windows.h>
#include <cstdio>
#include <vector>
#include <memory>
#include <string>
#include <sstream>
#include <cstdint>
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
struct FfmpegPipe {
    HANDLE hProc = NULL;
    HANDLE hStdinWrite = NULL; // 我们写入
    HANDLE hStdinRead  = NULL; // ffmpeg 的 stdin
    int w = 0, h = 0, fps = 30;
    std::string out_mp4;

    bool start(int width, int height, int fps_, const std::string& outdir) {
        stop();
        w = width; h = height; fps = fps_;
        out_mp4 = outdir + "capture.mp4";

        SECURITY_ATTRIBUTES sa{ sizeof(SECURITY_ATTRIBUTES) };
        sa.bInheritHandle = TRUE;
        sa.lpSecurityDescriptor = NULL;
        if (!CreatePipe(&hStdinRead, &hStdinWrite, &sa, 0)) return false;
        if (!SetHandleInformation(hStdinWrite, HANDLE_FLAG_INHERIT, 0)) return false;

        // 尝试 NVENC，失败可手动改为 libx264
        std::ostringstream cmd;
        cmd << "ffmpeg -loglevel error -y "
            << "-f rawvideo -pix_fmt bgra -s " << w << "x" << h << " -r " << fps << " -i pipe:0 "
            << "-c:v h264_nvenc -preset p5 -rc vbr -b:v 10M -maxrate 20M -pix_fmt yuv420p -movflags +faststart "
            << "\"" << out_mp4 << "\"";

        STARTUPINFOA si{}; si.cb = sizeof(si);
        si.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
        si.wShowWindow = SW_HIDE;
        si.hStdInput = hStdinRead;
        si.hStdError = GetStdHandle(STD_ERROR_HANDLE);
        si.hStdOutput = GetStdHandle(STD_OUTPUT_HANDLE);
        PROCESS_INFORMATION pi{};

        std::string cmdline = cmd.str();
        BOOL ok = CreateProcessA(
            NULL,
            cmdline.data(),
            NULL, NULL,
            TRUE, CREATE_NO_WINDOW,
            NULL, NULL,
            &si, &pi);
        if (!ok) {
            // 回退到 libx264
            std::ostringstream cmd2;
            cmd2 << "ffmpeg -loglevel error -y "
                 << "-f rawvideo -pix_fmt bgra -s " << w << "x" << h << " -r " << fps << " -i pipe:0 "
                 << "-c:v libx264 -preset veryfast -crf 18 -pix_fmt yuv420p -movflags +faststart "
                 << "\"" << out_mp4 << "\"";
            cmdline = cmd2.str();
            ok = CreateProcessA(NULL, cmdline.data(), NULL, NULL, TRUE, CREATE_NO_WINDOW, NULL, NULL, &si, &pi);
            if (!ok) return false;
        }
        CloseHandle(pi.hThread);
        hProc = pi.hProcess;
        return true;
    }
    bool write_frame_bgra(const uint8_t* data, size_t stride_bytes, int width, int height) {
		if (!hStdinWrite) return false;
		const size_t row_bytes = static_cast<size_t>(width) * 4;
		if (stride_bytes == row_bytes) {
			DWORD written = 0;
			const DWORD total = static_cast<DWORD>(row_bytes * height); // 修正 C4267
			if (!WriteFile(hStdinWrite, data, total, &written, NULL)) return false;
			return written == total;
		} else {
			std::vector<uint8_t> tight;
			tight.resize(row_bytes * height);
			for (int y = 0; y < height; ++y) {
				memcpy(tight.data() + y * row_bytes, data + y * stride_bytes, row_bytes);
			}
			DWORD written = 0;
			const DWORD total = static_cast<DWORD>(tight.size());
			if (!WriteFile(hStdinWrite, tight.data(), total, &written, NULL)) return false;
			return written == total;
		}
	}
    void stop() {
        if (hStdinWrite) { CloseHandle(hStdinWrite); hStdinWrite = NULL; }
        if (hStdinRead)  { CloseHandle(hStdinRead);  hStdinRead  = NULL; }
        if (hProc) { WaitForSingleObject(hProc, 5000); CloseHandle(hProc); hProc = NULL; }
        w = h = 0; out_mp4.clear();
    }
};
static FfmpegPipe g_ff;

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
            const std::string dirname = std::string("actions_") + get_datestr_yyyy_mm_dd() + "_" + std::to_string(now_us) + "/";
            g_rec_dir = shdata.output_filepath_creates_outdir_if_needed(dirname);
            const std::string csvpth = g_rec_dir + "actions.csv";
            g_actions_csv = fopen(csvpth.c_str(), "w");
            if (g_actions_csv) { fprintf(g_actions_csv, "frame_idx,time_us,keymask\n"); }
            g_recording = true;
            g_streaming = true;
            g_rec_idx = 0;
            g_last_cap_us = 0;
            g_fail_in_row = 0;
            reshade::log_message(reshade::log_level::info, ("REC start: " + g_rec_dir).c_str());
        }
        if (ctrl_down && runtime->is_key_pressed(VK_F10) && g_recording) {
            g_recording = false;
            g_streaming = false;
            g_ff.stop();
            if (g_actions_csv) { fclose(g_actions_csv); g_actions_csv = nullptr; }
            reshade::log_message(reshade::log_level::info, "REC stop");
        }

        if (g_recording && g_streaming) {
            const int64_t min_interval_us = 1000000 / std::max(1, g_video_fps);
            if (g_last_cap_us == 0 || (now_us - g_last_cap_us) >= min_interval_us) {
                // 键盘状态
                uint32_t keymask = 0;
                if (GetAsyncKeyState('W') & 0x8000) keymask |= 1u << 0;
                if (GetAsyncKeyState('A') & 0x8000) keymask |= 1u << 1;
                if (GetAsyncKeyState('S') & 0x8000) keymask |= 1u << 2;
                if (GetAsyncKeyState('D') & 0x8000) keymask |= 1u << 3;
                if (GetAsyncKeyState(VK_SHIFT) & 0x8000) keymask |= 1u << 4;
                if (GetAsyncKeyState(VK_SPACE) & 0x8000) keymask |= 1u << 5;

                // GPU->CPU 读回一帧 BGRA
                reshade::api::device* const dev = runtime->get_device();
                reshade::api::command_queue* const q = runtime->get_command_queue();
                const reshade::api::resource color_res = dev->get_resource_from_view(rtv);

                if (color_res == 0) {
                    reshade::log_message(reshade::log_level::warning, "stream skip: color resource null");
                } else {
                    std::unique_ptr<uint8_t[]> cpu_ptr;
                    size_t stride = 0; int w = 0, h = 0;
                    if (copy_color_to_cpu_bgra(dev, q, color_res, cpu_ptr, stride, w, h)) {
                        g_copy_fail_in_row = 0;
                        if (!g_ff.hProc) {
                            if (!g_ff.start(w, h, g_video_fps, g_rec_dir)) {
                                reshade::log_message(reshade::log_level::error, "ffmpeg start failed; stop streaming");
                                g_streaming = false;
                            }
                        }
                        if (g_ff.hProc) {
                            if (!g_ff.write_frame_bgra(cpu_ptr.get(), stride, w, h)) {
                                g_fail_in_row++;
                                reshade::log_message(reshade::log_level::error, "ffmpeg write frame failed");
                                if (g_fail_in_row >= g_fail_stop_threshold) {
                                    reshade::log_message(reshade::log_level::error, "too many failures; stop streaming");
                                    g_streaming = false;
                                    g_ff.stop();
                                }
                            } else {
                        g_copy_fail_in_row++;
                        reshade::log_message(reshade::log_level::warning, "stream copy to CPU failed");
                        if (g_copy_fail_in_row >= g_copy_fail_stop_threshold) {
                            reshade::log_message(reshade::log_level::error, "copy failed too many times; stop streaming");
                            g_streaming = false;
                            g_ff.stop();
                        }
                    }
                }
                g_last_cap_us = now_us;
            }
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
