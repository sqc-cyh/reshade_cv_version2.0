#
# Copyright(c) 2022 Intel. Licensed under the MIT License <http://opensource.org/licenses/MIT>.
#

# Before running this file, rename user_config.yaml.example -> user_config.yaml and modify it with appropriate paths for your system.

import argparse
import matplotlib.pyplot as plt
import numpy as np
import os
import pandas as pd
import shutil
import spear
import sys
import cv2
import csv
import json

component_descs = \
[
    {
        "name": "final_tone_curve_hdr_component",
        "long_name": "DefaultSceneRoot.final_tone_curve_hdr_",
        "visualize_func": lambda data : data
    },
    {
        "name": "normal",
        "long_name": "DefaultSceneRoot.normal_",
        "visualize_func": lambda data : np.clip(((data + 1.0) / 2.0)[:,:,0:3], 0.0, 1.0)
    },
    {
        "name": "depth",
        "long_name": "DefaultSceneRoot.depth_",
        "visualize_func": lambda data : data[:,:,0]
    }
]

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


    # create output dir
    for component_desc in component_descs:
        component_dir = os.path.realpath(os.path.join(os.path.dirname(__file__), "images", component_desc["name"]))
        if os.path.exists(component_dir):
            spear.log("Directory exists, removing: ", component_dir)
            shutil.rmtree(component_dir, ignore_errors=True)
        os.makedirs(component_dir, exist_ok=True)

    # get camera poses
    camera_poses_dir = os.path.realpath(os.path.join(os.path.dirname(__file__), "camera_poses"))
    camera_poses_file = os.path.realpath(os.path.join(camera_poses_dir, f"camera_poses.csv"))
    # df = pd.read_csv(camera_poses_file)

    # create instance
    config = spear.get_config(user_config_files=[os.path.realpath(os.path.join(os.path.dirname(__file__), "user_config.yaml"))])
    spear.configure_system(config=config)
    instance = spear.Instance(config=config)
    game = instance.get_game()

    # initialize actors and components
    with instance.begin_frame():

        # find functions
        actor_static_class = game.unreal_service.get_static_class(class_name="AActor")
        set_actor_location_func = game.unreal_service.find_function_by_name(uclass=actor_static_class, function_name="K2_SetActorLocation")
        set_actor_rotation_func = game.unreal_service.find_function_by_name(uclass=actor_static_class, function_name="K2_SetActorRotation")

        sp_scene_capture_component_2d_static_class = game.unreal_service.get_static_class(class_name="USpSceneCaptureComponent2D")
        initialize_func = game.unreal_service.find_function_by_name(uclass=sp_scene_capture_component_2d_static_class, function_name="Initialize")
        terminate_func = game.unreal_service.find_function_by_name(uclass=sp_scene_capture_component_2d_static_class, function_name="Terminate")

        # spawn camera sensor and get the final_tone_curve_hdr component
        bp_camera_sensor_uclass = game.unreal_service.load_object(class_name="UClass", outer=0, name="/SpComponents/Blueprints/BP_Camera_Sensor.BP_Camera_Sensor_C")
        bp_camera_sensor_actor = game.unreal_service.spawn_actor_from_class(uclass=bp_camera_sensor_uclass)

        # initialize components and get handles to their shared memory
        for component_desc in component_descs:
            component_desc["component"] = game.unreal_service.get_component_by_name(class_name="USceneComponent", actor=bp_camera_sensor_actor, component_name=component_desc["long_name"])
            game.unreal_service.call_function(uobject=component_desc["component"], ufunction=initialize_func)
            component_desc["shared_memory_handles"] = instance.sp_func_service.create_shared_memory_handles_for_object(uobject=component_desc["component"])
            get_prop = game.unreal_service.find_property_by_name_on_object
            if component_desc["name"] == "final_tone_curve_hdr_component":
                fov_deg  = game.unreal_service.get_property_value(get_prop(component_desc["component"], "FOVAngle"))
                W        = game.unreal_service.get_property_value(get_prop(component_desc["component"], "Width"))
                H        = game.unreal_service.get_property_value(get_prop(component_desc["component"], "Height"))
    with instance.end_frame():
        pass
    camera_poses = load_camera_poses_from_csv(camera_poses_file)

    for camera_pose in camera_poses:

        with instance.begin_frame():

            # set camera pose
            game.unreal_service.call_function(
                uobject=bp_camera_sensor_actor,
                ufunction=set_actor_location_func,
                args={"NewLocation": camera_pose['location']})

            game.unreal_service.call_function(
                uobject=bp_camera_sensor_actor,
                ufunction=set_actor_rotation_func,
                args={"NewRotation": camera_pose["rotation"]})
            
        #
        # let temporal anti-aliasing etc accumulate additional information across multiple frames
        #

        with instance.end_frame():
            pass

        for i in range(1):
            with instance.begin_frame():
                pass
            with instance.end_frame():
                pass

        with instance.begin_frame():
            pass

        with instance.end_frame():
            rgb_data = None
            depth_data = None
            # get rendered frame
            for component_desc in component_descs:
                return_values = instance.sp_func_service.call_function(
                    uobject=component_desc["component"],
                    function_name="read_pixels",
                    uobject_shared_memory_handles=component_desc["shared_memory_handles"])
                data = return_values["arrays"]["data"]

                # spear.log("component: ", component_desc["name"])
                # spear.log("shape:     ", return_values["arrays"]["data"].shape)
                # spear.log("dtype:     ", return_values["arrays"]["data"].dtype)
                # spear.log("min:       ", np.min(return_values["arrays"]["data"]))
                # spear.log("max:       ", np.max(return_values["arrays"]["data"]))

                component_dir = os.path.realpath(os.path.join(os.path.dirname(__file__), "images", component_desc["name"]))
                image_file = os.path.realpath(os.path.join(component_dir, "%04d.png"%camera_pose["index"]))
                image = component_desc["visualize_func"](return_values["arrays"]["data"])

                if component_desc["name"] == "final_tone_curve_hdr_component":
                    rgb_data = image
                elif component_desc["name"] == "depth":
                    depth_data = image
                # if component_desc["name"] == "depth":
                #     np.save(image_file, image)
                # else:
                #     cv2.imwrite(image_file, image)

            camera_pose = {
            'location': camera_pose['location'],
            'rotation': camera_pose['rotation'],
            'fov': fov_deg,
            'aspect_ratio': W / H
            }
            saved_files = save_depth_and_rgb_data(rgb_data, depth_data, camera_pose=camera_pose)

    # terminate actors and components
    with instance.begin_frame():
        pass
    with instance.end_frame():
        for component_desc in component_descs:
            instance.sp_func_service.destroy_shared_memory_handles_for_object(shared_memory_handles=component_desc["shared_memory_handles"])
            game.unreal_service.call_function(uobject=component_desc["component"], ufunction=terminate_func)
        game.unreal_service.destroy_actor(actor=bp_camera_sensor_actor)

    instance.close()

    spear.log("Done.")