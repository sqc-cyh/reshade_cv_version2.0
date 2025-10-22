#include <ShlObj.h>
#include <Windows.h>
#include <cnpy.h>
#include <imgui.h>
#include <process.h>

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <memory>
#include <nlohmann/json.hpp>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#include "copy_texture_into_packedbuf.h"
#include "gcv_games/game_interface_factory.h"
#include "gcv_utils/miscutils.h"
#include "generic_depth_struct.h"
#include "grabbers.h"
#include "hud_renderer.h"
#include "image_writer_thread_pool.h"
#include "recorder.h"
#include "render_target_stats/render_target_stats_tracking.hpp"
#include "segmentation/reshade_hooks.hpp"
#include "segmentation/segmentation_app_data.hpp"
#include "tex_buffer_utils.h"
using Json = nlohmann::json_abi_v3_12_0::json;
typedef std::chrono::steady_clock hiresclock;

// ------------------ Global recording status ------------------
static int g_recording_mode = 0;  // 0: not recording, 1: depth mode, 2: controls mode
static int g_video_fps = 1;
static std::unique_ptr<Recorder> g_rec;
static std::string g_rec_dir;

static FILE* g_actions_csv = nullptr;
// It is only used to determine whether a header needs to be written. It is actually written in Recorder

static uint64_t g_rec_idx = 0;
static int64_t g_last_cap_us = 0;
static int g_copy_fail_in_row = 0;
static const int g_copy_fail_stop_threshold = 60;
static DepthToneParams g_depth_tone;  // clip/log parameter

static void on_init(reshade::api::device* device) {
    auto& shdata = device->create_private_data<image_writer_thread_pool>();
    reshade::log_message(reshade::log_level::info, std::string(std::string("tests: ") + run_utils_tests()).c_str());
    shdata.init_time = hiresclock::now();
}
static void on_destroy(reshade::api::device* device) {
    device->get_private_data<image_writer_thread_pool>().change_num_threads(0);
    device->get_private_data<image_writer_thread_pool>().print_waiting_log_messages();

    if (g_rec) {
        g_rec->stop();
        g_rec.reset();
    }
    if (g_actions_csv) {
        fclose(g_actions_csv);
        g_actions_csv = nullptr;
    }

    device->destroy_private_data<image_writer_thread_pool>();
}

// ------------------  Recording ------------------
static void on_reshade_finish_effects(reshade::api::effect_runtime* runtime,
                                      reshade::api::command_list*, reshade::api::resource_view rtv, reshade::api::resource_view) {
    auto& shdata = runtime->get_device()->get_private_data<image_writer_thread_pool>();
    CamMatrixData gamecam;
    std::string errstr;
    bool shaderupdatedwithcampos = false;
    float shadercamposbuf[4];
    reshade::api::device* const device = runtime->get_device();
    auto& segmapp = device->get_private_data<segmentation_app_data>();

    {  // record
        const int64_t now_us = std::chrono::duration_cast<std::chrono::microseconds>(hiresclock::now() - shdata.init_time).count();
        const bool ctrl_down = (GetAsyncKeyState(VK_CONTROL) & 0x8000) != 0;

        // start record
        if (ctrl_down && g_recording_mode == 0) {
            bool start_rec = false;
            if (runtime->is_key_pressed(VK_F9)) {
                // Logic 1: 5fps, depth, rgb video, camera
                g_recording_mode = 1;
                g_video_fps = 1;
                start_rec = true;
            } else if (runtime->is_key_pressed(VK_F7)) {
                // Logic 2: 16fps, rgb video, controls, camera
                g_recording_mode = 2;
                g_video_fps = 16;
                start_rec = true;
            }

            if (start_rec) {
                const std::string dirname = std::string("actions_") + get_datestr_yyyy_mm_dd() + "_" + std::to_string(now_us) + "/";
                g_rec_dir = shdata.output_filepath_creates_outdir_if_needed(dirname);

                RecorderConfig cfg{g_video_fps, g_rec_dir, true};  // constructor init
                g_rec = std::make_unique<Recorder>(cfg);
                g_rec->start();

                g_rec_idx = 0;
                g_last_cap_us = 0;
                g_copy_fail_in_row = 0;

                reshade::log_message(reshade::log_level::info, ("REC start (mode " + std::to_string(g_recording_mode) + "): " + g_rec_dir).c_str());
            }
        }

        // stop record
        if (ctrl_down && (runtime->is_key_pressed(VK_F10) || runtime->is_key_pressed(VK_F8)) && g_recording_mode != 0) {
            g_recording_mode = 0;
            if (g_rec) {
                g_rec->stop();
                g_rec.reset();
            }
            if (g_actions_csv) {
                fclose(g_actions_csv);
                g_actions_csv = nullptr;
            }
            reshade::log_message(reshade::log_level::info, "REC stop");
        }

        // recording
        if (g_recording_mode != 0) {
            const int fps = std::max(1, g_video_fps);
            const int64_t period_us = 1000000LL / fps;
            static int64_t next_due_us = 0;
            if (g_last_cap_us == 0) {
                g_last_cap_us = now_us;
                next_due_us = now_us;
            }

            if (now_us >= next_due_us) {
                bool delta_depth_ok = true;
                bool delta_control_ok = true;

                // 彩色帧

                reshade::api::device* const dev = runtime->get_device();
                reshade::api::command_queue* const q = runtime->get_command_queue();
                const reshade::api::resource color_res = dev->get_resource_from_view(rtv);

                bool color_ok = false;
                int w = 0, h = 0;

                if (color_res.handle == 0) {
                    reshade::log_message(reshade::log_level::warning, "stream skip: color resource null");
                } else {
                    std::vector<uint8_t> bgra;

                    // camera position
                    const int64_t now_us_control_1 = std::chrono::duration_cast<std::chrono::microseconds>(hiresclock::now() - shdata.init_time).count();
                    CamMatrixData cam;
                    std::string cam_err;
                    const auto cam_ok = shdata.get_camera_matrix(cam, cam_err);
                    Json camj;
                    if (cam_ok) {
                        cam.into_json(camj);
                    } else {
                        camj["cam_status"] = "uninitialized";
                        if (!cam_err.empty()) camj["err"] = cam_err;
                    }

                    const int64_t now_us_control_2 = std::chrono::duration_cast<std::chrono::microseconds>(hiresclock::now() - shdata.init_time).count();
                    const int64_t delta_us_control = now_us_control_2 - now_us_control_1;
                    reshade::log_message(reshade::log_level::info,
                                         ("Frame delta: Δt=%lld us", std::to_string(delta_us_control).c_str()));
                    if (std::abs(delta_us_control) > 5000) {
                        delta_control_ok = false;
                        reshade::log_message(reshade::log_level::info,
                                             ("Frame skipped: Δt=%lld us", std::to_string(delta_us_control).c_str()));
                    }

                    const int64_t now_us_depth_1 = std::chrono::duration_cast<std::chrono::microseconds>(hiresclock::now() - shdata.init_time).count();

                    // Logic 1: save depth data
                    if (g_recording_mode == 1) {
                        generic_depth_data& genericdepdata = runtime->get_private_data<generic_depth_data>();
                        reshade::api::resource depth_res = genericdepdata.selected_depth_stencil;
                        reshade::api::command_queue* const q2 = runtime->get_command_queue();

                        if (depth_res.handle != 0) {
                            char basebuf[512];
                            _snprintf_s(basebuf, _TRUNCATE, "%s/frame_%06llu_",
                                        g_rec_dir.c_str(), (unsigned long long)g_rec_idx);
                            const std::string basefilen = std::string(basebuf);

                            uint32_t writers = ImageWriter_numpy;  // 生成 depth.npy
                            // ImageWriter_STB_png
                            const bool ok_depth =
                                shdata.save_texture_image_needing_resource_barrier_copy(
                                    basefilen + "depth",
                                    writers,
                                    q2,
                                    depth_res,
                                    TexInterp_Depth);
                            if (!ok_depth) {
                                reshade::log_message(reshade::log_level::warning,
                                                     "record: failed to save per-frame depth (.npy/.fpzip)");
                            }
                        }
                    }

                    const int64_t now_us_depth_2 = std::chrono::duration_cast<std::chrono::microseconds>(hiresclock::now() - shdata.init_time).count();
                    const int64_t delta_us_depth = now_us_depth_2 - now_us_depth_1;
                    reshade::log_message(reshade::log_level::info,
                                         ("Frame delta: Δt=%lld us", std::to_string(delta_us_depth).c_str()));
                    if (std::abs(delta_us_depth) > 50000) {
                        delta_depth_ok = false;
                        reshade::log_message(reshade::log_level::info,
                                             ("Frame skipped: Δt=%lld us", std::to_string(delta_us_depth).c_str()));
                    }

                    // keyboard status
                    uint32_t keymask_letters = 0;    // bit 0='A', bit 1='B', ..., bit 25='Z'
                    uint32_t keymask_modifiers = 0;  // 其他控制键

                    // 记录 A-Z
                    for (int i = 0; i < 26; ++i) {
                        char vk = 'A' + i;
                        if (GetAsyncKeyState(vk) & 0x8000) {
                            keymask_letters |= (1u << i);
                        }
                    }

                    // 控制键
                    const int SHIFT_BIT = 0;
                    const int CTRL_BIT = 1;
                    const int ALT_BIT = 2;
                    const int SPACE_BIT = 3;
                    const int ENTER_BIT = 4;
                    const int ESCAPE_BIT = 5;
                    const int TAB_BIT = 6;

                    if (GetAsyncKeyState(VK_SHIFT) & 0x8000) keymask_modifiers |= (1u << SHIFT_BIT);
                    if (GetAsyncKeyState(VK_CONTROL) & 0x8000) keymask_modifiers |= (1u << CTRL_BIT);
                    if (GetAsyncKeyState(VK_MENU) & 0x8000) keymask_modifiers |= (1u << ALT_BIT);  // VK_MENU = Alt
                    if (GetAsyncKeyState(VK_SPACE) & 0x8000) keymask_modifiers |= (1u << SPACE_BIT);
                    if (GetAsyncKeyState(VK_RETURN) & 0x8000) keymask_modifiers |= (1u << ENTER_BIT);
                    if (GetAsyncKeyState(VK_ESCAPE) & 0x8000) keymask_modifiers |= (1u << ESCAPE_BIT);
                    if (GetAsyncKeyState(VK_TAB) & 0x8000) keymask_modifiers |= (1u << TAB_BIT);

                    if (grab_bgra_frame(q, color_res, bgra, w, h)) {
                        g_copy_fail_in_row = 0;
                        g_rec->push_color(bgra.data(), w, h);
                        color_ok = true;
                    }

                    if (delta_depth_ok && delta_control_ok) {
                        g_rec->log_camera_json(/*idx=*/g_rec_idx,
                                               /*time_us=*/now_us,
                                               /*cam_json=*/camj,
                                               /*img_w=*/w, /*img_h=*/h);
                    }

                    if (g_recording_mode == 2) {  // Logic 2: save control signals
                        g_rec->log_action(g_rec_idx, now_us, keymask_letters, keymask_modifiers);
                    }
                    ++g_rec_idx;
                }

                next_due_us += period_us;
                g_last_cap_us = now_us;
            }
            return;
        }
    }

    if (segmentation_app_update_on_finish_effects(runtime, runtime->is_key_pressed(VK_F11))) {
        generic_depth_data& genericdepdata = runtime->get_private_data<generic_depth_data>();
        reshade::api::command_queue* cmdqueue = runtime->get_command_queue();
        const int64_t microseconds_elapsed = std::chrono::duration_cast<std::chrono::microseconds>(hiresclock::now() - shdata.init_time).count();
        const std::string microelapsedstr = std::to_string(microseconds_elapsed);
        const std::string basefilen = shdata.gamename_simpler() + std::string("_") + get_datestr_yyyy_mm_dd() + std::string("_") + microelapsedstr + std::string("_");
        std::stringstream capmessage;
        capmessage << "capture " << basefilen << ": ";
        bool capgood = true;
        nlohmann::json metajson;

#if RENDERDOC_FOR_SHADERS
        if (shdata.depth_settings.more_verbose || shdata.depth_settings.debug_mode) {
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
        if (g_recording_mode == 0) {
            const int64_t now_us_depth_11 = std::chrono::duration_cast<std::chrono::microseconds>(hiresclock::now() - shdata.init_time).count();
            if (shdata.save_texture_image_needing_resource_barrier_copy(basefilen + std::string("RGB"),
                                                                        ImageWriter_STB_png, cmdqueue, device->get_resource_from_view(rtv), TexInterp_RGB)) {
                const int64_t now_us_depth_21 = std::chrono::duration_cast<std::chrono::microseconds>(hiresclock::now() - shdata.init_time).count();
                const int64_t delta_us_depth1 = now_us_depth_21 - now_us_depth_11;
                reshade::log_message(reshade::log_level::info,
                                     ("Frame delta1111: Δt=%lld us", std::to_string(delta_us_depth1).c_str()));
                if (std::abs(delta_us_depth1) > 6000) {
                    // delta_depth_ok = false;
                    reshade::log_message(reshade::log_level::info,
                                         ("Frame skipped1111: Δt=%lld us", std::to_string(delta_us_depth1).c_str()));
                }
                if (shdata.save_texture_image_needing_resource_barrier_copy(basefilen + std::string("depth"),
                                                                            ImageWriter_STB_png | ImageWriter_epr | ImageWriter_numpy | (shdata.game_knows_depthbuffer() ? ImageWriter_fpzip : 0),
                                                                            cmdqueue, genericdepdata.selected_depth_stencil, TexInterp_Depth)) {
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
    if (shdata.grabcamcoords) {
        if (gamecam.extrinsic_status == CamMatrix_Uninitialized) {
            shdata.get_camera_matrix(gamecam, errstr);
        }
        if (gamecam.extrinsic_status != CamMatrix_Uninitialized) {
            shadercamposbuf[3] = 1.0f;
            if (gamecam.extrinsic_status & CamMatrix_PositionGood || gamecam.extrinsic_status & CamMatrix_WIP) {
                for (int ii = 0; ii < 3; ++ii) shadercamposbuf[ii] = gamecam.extrinsic_cam2world(ii, cam_matrix_position_column);
                runtime->set_uniform_value_float(runtime->find_uniform_variable("displaycamcoords.fx", "dispcam_latestcampos"), shadercamposbuf, 4);
                shaderupdatedwithcampos = true;
            }
            if (gamecam.extrinsic_status & CamMatrix_RotationGood || gamecam.extrinsic_status & CamMatrix_WIP) {
                for (int colidx = 0; colidx < 3; ++colidx) {
                    for (int ii = 0; ii < 3; ++ii) shadercamposbuf[ii] = gamecam.extrinsic_cam2world(ii, colidx);
                    runtime->set_uniform_value_float(runtime->find_uniform_variable("displaycamcoords.fx", (std::string("dispcam_latestcamcol") + std::to_string(colidx)).c_str()), shadercamposbuf, 4);
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

static void draw_settings_overlay(reshade::api::effect_runtime* runtime) {
    auto& shdata = runtime->get_device()->get_private_data<image_writer_thread_pool>();
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
        CamMatrixData lcam;
        std::string errstr;
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

extern "C" __declspec(dllexport) const char* NAME = "CV Capture";
extern "C" __declspec(dllexport) const char* DESCRIPTION =
    "Add-on that captures the screen after effects were rendered, and also the depth buffer, every time key is pressed.";

BOOL WINAPI DllMain(HINSTANCE hinstDLL, DWORD fdwReason, LPVOID) {
    switch (fdwReason) {
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
// Copyright (C) 2022 Jason Bunk