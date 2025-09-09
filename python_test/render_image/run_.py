#
# Copyright(c) 2022 Intel. Licensed under the MIT License <http://opensource.org/licenses/MIT>.
#

# Before running this file, rename user_config.yaml.example -> user_config.yaml and modify it with appropriate paths for your system.

import argparse
import cv2
import math
import os
import spear
import time
import csv
import json
def save_depth_and_rgb_data(rgb_data, depth_data, camera_pose, output_dir="output", filename_prefix="render"):
    """
    保存RGB和深度数据到文件
    
    Args:
        rgb_data: RGB图像数据 (numpy array)
        depth_data: 深度数据 (numpy array) 
        camera_pose: 相机姿态信息 (dict)
        output_dir: 输出目录
        filename_prefix: 文件名前缀
    """
    import numpy as np
    from datetime import datetime
    
    # 创建输出目录
    os.makedirs(output_dir, exist_ok=True)
    
    # 生成时间戳
    timestamp = datetime.now().strftime("%Y%m%d_%H%M%S")
    
    # 保存RGB图像
    rgb_filename = os.path.join(output_dir, f"{filename_prefix}_rgb_{timestamp}.png")
    cv2.imwrite(rgb_filename, rgb_data)
    # spear.log(f"RGB图像已保存到: {rgb_filename}")
    
    # 保存深度数据为numpy数组
    depth_filename = os.path.join(output_dir, f"{filename_prefix}_depth_{timestamp}.npy")
    np.save(depth_filename, depth_data)
    # spear.log(f"深度数据已保存到: {depth_filename}")
    
    # 保存深度数据为可视化图像
    depth_viz_filename = os.path.join(output_dir, f"{filename_prefix}_depth_viz_{timestamp}.png")
    
    # 归一化深度数据用于可视化
    if depth_data.size > 0:
        depth_min = np.min(depth_data)
        depth_max = np.max(depth_data)
        if depth_max > depth_min:
            depth_normalized = ((depth_data - depth_min) / (depth_max - depth_min) * 255).astype(np.uint8)
            cv2.imwrite(depth_viz_filename, depth_normalized)
            # spear.log(f"深度可视化图像已保存到: {depth_viz_filename}")
    
    # 保存相机pose为JSON格式
    import json
    pose_filename = os.path.join(output_dir, f"{filename_prefix}_pose_{timestamp}.json")
    with open(pose_filename, 'w', encoding='utf-8') as f:
        json.dump(camera_pose, f, indent=4, ensure_ascii=False)
    # spear.log(f"相机pose已保存到: {pose_filename}")
    
    # 保存相机pose为numpy格式 (用于方便的数值计算)
    pose_npy_filename = os.path.join(output_dir, f"{filename_prefix}_pose_{timestamp}.npy")
    pose_array = {
        'location': np.array([camera_pose['location']['x'], camera_pose['location']['y'], camera_pose['location']['z']]),
        'rotation': np.array([camera_pose['rotation']['pitch'], camera_pose['rotation']['yaw'], camera_pose['rotation']['roll']]),
        'fov': camera_pose['fov'],
        'aspect_ratio': camera_pose['aspect_ratio']
    }
    np.savez(pose_npy_filename, **pose_array)
    # spear.log(f"相机pose数组已保存到: {pose_npy_filename}")
    
    # 保存元数据
    metadata_filename = os.path.join(output_dir, f"{filename_prefix}_metadata_{timestamp}.txt")
    with open(metadata_filename, 'w', encoding='utf-8') as f:
        f.write(f"保存时间: {datetime.now().strftime('%Y-%m-%d %H:%M:%S')}\n")
        f.write(f"RGB数据形状: {rgb_data.shape}\n")
        f.write(f"深度数据形状: {depth_data.shape}\n")
        f.write(f"RGB数据类型: {rgb_data.dtype}\n")
        f.write(f"深度数据类型: {depth_data.dtype}\n")
        if depth_data.size > 0:
            f.write(f"深度范围: {np.min(depth_data):.6f} - {np.max(depth_data):.6f}\n")
        
        # 添加相机pose信息到元数据
        f.write(f"\n相机Pose信息:\n")
        f.write(f"位置 (x, y, z): {camera_pose['location']['x']:.6f}, {camera_pose['location']['y']:.6f}, {camera_pose['location']['z']:.6f}\n")
        f.write(f"旋转 (pitch, yaw, roll): {camera_pose['rotation']['pitch']:.6f}, {camera_pose['rotation']['yaw']:.6f}, {camera_pose['rotation']['roll']:.6f}\n")
        f.write(f"视场角 (FOV): {camera_pose['fov']:.6f} degrees\n")
        f.write(f"宽高比: {camera_pose['aspect_ratio']:.6f}\n")
    
    spear.log(f"元数据已保存到: {metadata_filename}")
    
    return {
        'rgb_file': rgb_filename,
        'depth_file': depth_filename,
        'depth_viz_file': depth_viz_filename,
        'pose_json_file': pose_filename,
        'pose_npy_file': pose_npy_filename,
        'metadata_file': metadata_filename
    }


def load_camera_poses_from_csv(csv_file):
    """
    从CSV文件加载相机poses
    
    Args:
        csv_file: CSV文件路径
        
    Returns:
        camera_poses: 相机poses列表
    """
    camera_poses = []
    
    with open(csv_file, 'r', encoding='utf-8') as f:
        reader = csv.DictReader(f)
        for row in reader:
            # 解析JSON字符串格式的location和rotation
            location = json.loads(row['location'])
            rotation = json.loads(row['rotation'])
            
            camera_pose = {
                'index': int(row['index']),
                'location': location,
                'rotation': rotation,
                'fov': float(row['fov']),
                'aspect_ratio': float(row['aspect_ratio']),
                'viewport_x': float(row['viewport_x']),
                'viewport_y': float(row['viewport_y'])
            }
            camera_poses.append(camera_pose)
    
    spear.log(f"从CSV文件加载了 {len(camera_poses)} 个相机poses")
    return camera_poses


if __name__ == "__main__":

    # create instance
    config = spear.get_config(user_config_files=[os.path.realpath(os.path.join(os.path.dirname(__file__), "user_config.yaml"))])
    spear.configure_system(config=config)
    instance = spear.Instance(config=config)
    game = instance.get_game()
    csv_file = os.path.realpath(os.path.join(os.path.dirname(__file__),r"camera_poses\camera_poses.csv"))
    if not os.path.exists(csv_file):
        spear.log(f"错误: CSV文件不存在: {csv_file}")
        spear.log("请先运行 generate_camera_poses.py 生成相机poses")
        exit(1)
    
    # 加载相机poses
    camera_poses = load_camera_poses_from_csv(csv_file)
    # initialize actors and components
    with instance.begin_frame():

        # find functions
        actor_static_class = game.unreal_service.get_static_class(class_name="AActor")
        set_actor_location_func = game.unreal_service.find_function_by_name(uclass=actor_static_class, function_name="K2_SetActorLocation")
        set_actor_rotation_func = game.unreal_service.find_function_by_name(uclass=actor_static_class, function_name="K2_SetActorRotation")

        sp_scene_capture_component_2d_static_class = game.unreal_service.get_static_class(class_name="USpSceneCaptureComponent2D")
        initialize_func = game.unreal_service.find_function_by_name(uclass=sp_scene_capture_component_2d_static_class, function_name="Initialize")
        terminate_func = game.unreal_service.find_function_by_name(uclass=sp_scene_capture_component_2d_static_class, function_name="Terminate")

        gameplay_statics_static_class = game.unreal_service.get_static_class(class_name="UGameplayStatics")
        get_player_controller_func = game.unreal_service.find_function_by_name(uclass=gameplay_statics_static_class, function_name="GetPlayerController")

        # get UGameplayStatics default object
        gameplay_statics_default_object = game.unreal_service.get_default_object(uclass=gameplay_statics_static_class, create_if_needed=False)

        # spawn camera sensor and get the final_tone_curve_hdr component
        bp_camera_sensor_uclass = game.unreal_service.load_object(class_name="UClass", outer=0, name="/SpComponents/Blueprints/BP_Camera_Sensor.BP_Camera_Sensor_C")
        bp_camera_sensor_actor = game.unreal_service.spawn_actor_from_class(uclass=bp_camera_sensor_uclass)
        final_tone_curve_hdr_component = game.unreal_service.get_component_by_name(class_name="USceneComponent", actor=bp_camera_sensor_actor, component_name="DefaultSceneRoot.final_tone_curve_hdr_")
        final_depth = game.unreal_service.get_component_by_name(class_name="USceneComponent", actor=bp_camera_sensor_actor, component_name="DefaultSceneRoot.depth_")

        # configure the final_tone_curve_hdr component to match the viewport (width, height, FOV, post-processing settings, etc)

    with instance.end_frame():
        pass
        
    for i, camera_pose in enumerate(camera_poses):
        spear.log(f"渲染pose {i+1}/{len(camera_poses)} (索引: {camera_pose['index']})")
        with instance.begin_frame():
            post_process_volume = game.unreal_service.find_actor_by_type(class_name="APostProcessVolume")
            return_values = game.unreal_service.call_function(uobject=gameplay_statics_default_object, ufunction=get_player_controller_func, args={"PlayerIndex": 0})
            player_controller = spear.to_handle(string=return_values["ReturnValue"])
            player_camera_manager_desc = game.unreal_service.find_property_by_name_on_object(uobject=player_controller, property_name="PlayerCameraManager")
            player_camera_manager_string = game.unreal_service.get_property_value(property_desc=player_camera_manager_desc)
            player_camera_manager = spear.to_handle(string=player_camera_manager_string)

            viewport_size = instance.engine_service.get_viewport_size()
            viewport_x = viewport_size[0]
            viewport_y = viewport_size[1]

            viewport_aspect_ratio = viewport_x/viewport_y # see Engine/Source/Editor/UnrealEd/Private/EditorViewportClient.cpp:2130 for evidence that Unreal's aspect ratio convention is x/y

            view_target_pov_desc = game.unreal_service.find_property_by_name_on_object(uobject=player_camera_manager, property_name="ViewTarget.POV")
            view_target_pov = game.unreal_service.get_property_value(property_desc=view_target_pov_desc)

            fov = view_target_pov["fOV"]*math.pi/180.0 # this adjustment is necessary to compute an FOV value that matches the game viewport
            half_fov = fov/2.0
            half_fov_adjusted = math.atan(math.tan(half_fov)*viewport_aspect_ratio/view_target_pov["aspectRatio"])
            fov_adjusted = half_fov_adjusted*2.0
            fov_adjusted_degrees = fov_adjusted*180.0/math.pi

            volume_settings_desc = game.unreal_service.find_property_by_name_on_object(uobject=post_process_volume, property_name="Settings")
            volume_settings = game.unreal_service.get_property_value(property_desc=volume_settings_desc)

            game.unreal_service.call_function(uobject=bp_camera_sensor_actor, ufunction=set_actor_location_func, args={"NewLocation": camera_pose['location']})
            game.unreal_service.call_function(uobject=bp_camera_sensor_actor, ufunction=set_actor_rotation_func, args={"NewRotation": camera_pose['rotation']})

            for comp in [final_tone_curve_hdr_component, final_depth]:
                for name, val in [
                    ("Width", camera_pose['viewport_x']),
                    ("Height", camera_pose['viewport_y']),
                    ("FOVAngle", camera_pose['fov']),
                ]:
                    desc = game.unreal_service.find_property_by_name_on_object(
                        uobject=comp, property_name=name
                    )
                    if desc is not None:
                        game.unreal_service.set_property_value(property_desc=desc, property_value=val)

            # 若 depth 组件也有 PostProcessSettings（不一定有），可以一并设置
            # depth_pp_desc = game.unreal_service.find_property_by_name_on_object(
            #     uobject=final_depth, property_name="PostProcessSettings"
            # )
            # if depth_pp_desc is not None:
            #     game.unreal_service.set_property_value(property_desc=depth_pp_desc, property_value=volume_settings)

            # 再初始化与读像素
            game.unreal_service.call_function(uobject=final_tone_curve_hdr_component, ufunction=initialize_func)
            # configure_depth_component(game, final_depth)
            game.unreal_service.call_function(uobject=final_depth, ufunction=initialize_func)

            final_tone_curve_hdr_component_shared_memory_handles = instance.sp_func_service.create_shared_memory_handles_for_object(uobject=final_tone_curve_hdr_component)
            final_depth_shared_memory_handles = instance.sp_func_service.create_shared_memory_handles_for_object(uobject=final_depth)

            ret_rgb = instance.sp_func_service.call_function(
                uobject=final_tone_curve_hdr_component, function_name="read_pixels",
                uobject_shared_memory_handles=final_tone_curve_hdr_component_shared_memory_handles
            )
            ret_depth = instance.sp_func_service.call_function(
                uobject=final_depth, function_name="read_pixels",
                uobject_shared_memory_handles=final_depth_shared_memory_handles
            )
            rgb_data = ret_rgb["arrays"]["data"]
            depth_data = ret_depth["arrays"]["data"]
            usc_class = game.unreal_service.get_static_class(class_name="USceneComponent")
            get_loc_func = game.unreal_service.find_function_by_name(uclass=usc_class, function_name="K2_GetComponentLocation")
            get_rot_func = game.unreal_service.find_function_by_name(uclass=usc_class, function_name="K2_GetComponentRotation")

            loc_ret = game.unreal_service.call_function(uobject=final_tone_curve_hdr_component, ufunction=get_loc_func)
            rot_ret = game.unreal_service.call_function(uobject=final_tone_curve_hdr_component, ufunction=get_rot_func)
            sensor_loc = loc_ret["ReturnValue"]     # {x,y,z}（UE单位）
            sensor_rot = rot_ret["ReturnValue"]      # {pitch,yaw,roll}（度, UE Rotator）

            # === 2) 读这帧真正用于渲染的 FOV 和分辨率 ===
            get_prop = game.unreal_service.find_property_by_name_on_object
            Width  = game.unreal_service.get_property_value(get_prop(final_tone_curve_hdr_component, "Width"))
            Height = game.unreal_service.get_property_value(get_prop(final_tone_curve_hdr_component, "Height"))
            FOVdeg = game.unreal_service.get_property_value(get_prop(final_tone_curve_hdr_component, "FOVAngle"))
            aspect = float(Width) / float(Height)
            # # 获取camera pose信息
            camera_pose = {
                'location': camera_pose['location'],
                'rotation': camera_pose['rotation'],
                'fov': fov_adjusted_degrees,
                'aspect_ratio': viewport_aspect_ratio
            }
            saved_files = save_depth_and_rgb_data(rgb_data, depth_data, camera_pose=camera_pose)
        with instance.end_frame():
            pass
    

    # # terminate actors and components
    with instance.begin_frame():
        pass
    with instance.end_frame():
        instance.sp_func_service.destroy_shared_memory_handles_for_object(shared_memory_handles=final_tone_curve_hdr_component_shared_memory_handles)
        game.unreal_service.call_function(uobject=final_tone_curve_hdr_component, ufunction=terminate_func)
        game.unreal_service.destroy_actor(actor=bp_camera_sensor_actor)
        instance.sp_func_service.destroy_shared_memory_handles_for_object(shared_memory_handles=final_depth_shared_memory_handles)

    # spear.log("Done.")