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
typedef std::chrono::steady_clock hiresclock;
// 录制状态
static bool g_recording = false;
static uint64_t g_rec_idx = 0;
static std::string g_rec_dir;
static FILE* g_actions_csv = nullptr;
static int g_target_fps = 20;           // 录制 FPS
static int64_t g_last_cap_us = 0;

static void on_init(reshade::api::device* device)
{
	auto &shdata = device->create_private_data<image_writer_thread_pool>();
	reshade::log_message(reshade::log_level::info, std::string(std::string("tests: ")+run_utils_tests()).c_str());
	shdata.init_time = hiresclock::now();
}
static void on_destroy(reshade::api::device* device)
{
    device->get_private_data<image_writer_thread_pool>().change_num_threads(0);
    device->get_private_data<image_writer_thread_pool>().print_waiting_log_messages();

    // 确保关闭 CSV（即使未手动停止录制）
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
            if (g_actions_csv) {
                fprintf(g_actions_csv, "frame_idx,time_us,keymask\n");
            }
            g_recording = true;
            g_rec_idx = 0;
            g_last_cap_us = 0;
            reshade::log_message(reshade::log_level::info, ("REC start: " + g_rec_dir).c_str());
        }
        if (ctrl_down && runtime->is_key_pressed(VK_F10) && g_recording) {
            g_recording = false;
            if (g_actions_csv) { fclose(g_actions_csv); g_actions_csv = nullptr; }
            reshade::log_message(reshade::log_level::info, "REC stop");
        }

        if (g_recording) {
            const int64_t min_interval_us = 1000000 / std::max(1, g_target_fps);
            if (g_last_cap_us == 0 || (now_us - g_last_cap_us) >= min_interval_us) {
                // 键盘状态（bitmask: 0=W,1=A,2=S,3=D,4=Shift,5=Space）
                uint32_t keymask = 0;
                if (GetAsyncKeyState('W') & 0x8000) keymask |= 1u << 0;
                if (GetAsyncKeyState('A') & 0x8000) keymask |= 1u << 1;
                if (GetAsyncKeyState('S') & 0x8000) keymask |= 1u << 2;
                if (GetAsyncKeyState('D') & 0x8000) keymask |= 1u << 3;
                if (GetAsyncKeyState(VK_SHIFT) & 0x8000) keymask |= 1u << 4;
                if (GetAsyncKeyState(VK_SPACE) & 0x8000) keymask |= 1u << 5;

                // 保存 RGB / Depth（异步线程池写盘，减少卡顿）
                generic_depth_data &genericdepdata = runtime->get_private_data<generic_depth_data>();
                reshade::api::command_queue *cmdqueue = runtime->get_command_queue();

                char base[512];
                sprintf_s(base, "%sframe_%06llu_", g_rec_dir.c_str(), (unsigned long long)g_rec_idx);

                bool ok_rgb = shdata.save_texture_image_needing_resource_barrier_copy(
                    std::string(base) + "RGB",
                    ImageWriter_STB_png,
                    cmdqueue, device->get_resource_from_view(rtv),
                    TexInterp_RGB);

                bool ok_depth = shdata.save_texture_image_needing_resource_barrier_copy(
                    std::string(base) + "depth",
                    ImageWriter_STB_png | ImageWriter_numpy | (shdata.game_knows_depthbuffer() ? ImageWriter_fpzip : 0),
                    cmdqueue, genericdepdata.selected_depth_stencil,
                    TexInterp_Depth);

                if (g_actions_csv) {
                    fprintf(g_actions_csv, "%llu,%lld,%u\n",
                        (unsigned long long)g_rec_idx, (long long)now_us, keymask);
                }
                if (!(ok_rgb && ok_depth)) {
                    reshade::log_message(reshade::log_level::error, "record frame save failed");
                }

                g_rec_idx++;
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
