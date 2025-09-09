#
# Copyright(c) 2022 Intel. Licensed under the MIT License <http://opensource.org/licenses/MIT>.
#

# Before running this file, rename user_config.yaml.example -> user_config.yaml and modify it with appropriate paths for your system.

import argparse
import math
import numpy as np
import os
import pandas as pd
import shutil
import spear
import sys
import time


num_camera_poses = 50  # 生成的相机pose数量

if __name__ == "__main__":

    # create output dir
    camera_poses_dir = os.path.realpath(os.path.join(os.path.dirname(__file__), "camera_poses"))
    if os.path.exists(camera_poses_dir):
        spear.log("Directory exists, removing: ", camera_poses_dir)
        shutil.rmtree(camera_poses_dir, ignore_errors=True)
    os.makedirs(camera_poses_dir, exist_ok=True)

    # create instance
    config = spear.get_config(user_config_files=[os.path.realpath(os.path.join(os.path.dirname(__file__), "user_config.yaml"))])
    spear.configure_system(config=config)
    instance = spear.Instance(config=config)
    game = instance.get_game()

    spear.log("开始生成相机poses...")

    # 存储所有的camera poses
    camera_poses_list = []

    with instance.begin_frame():
        # 找到必要的函数和对象
        gameplay_statics_static_class = game.unreal_service.get_static_class(class_name="UGameplayStatics")
        get_player_controller_func = game.unreal_service.find_function_by_name(uclass=gameplay_statics_static_class, function_name="GetPlayerController")
        gameplay_statics_default_object = game.unreal_service.get_default_object(uclass=gameplay_statics_static_class, create_if_needed=False)

    with instance.end_frame():
        pass

    # 收集多个相机poses
    i = 0
    while i < num_camera_poses:
        time.sleep(1/10)  # 给用户时间移动相机

        with instance.begin_frame():
            # 获取当前玩家相机的信息
            return_values = game.unreal_service.call_function(uobject=gameplay_statics_default_object, ufunction=get_player_controller_func, args={"PlayerIndex": 0})
            player_controller = spear.to_handle(string=return_values["ReturnValue"])
            player_camera_manager_desc = game.unreal_service.find_property_by_name_on_object(uobject=player_controller, property_name="PlayerCameraManager")
            player_camera_manager_string = game.unreal_service.get_property_value(property_desc=player_camera_manager_desc)
            player_camera_manager = spear.to_handle(string=player_camera_manager_string)

            # 获取viewport信息
            viewport_size = instance.engine_service.get_viewport_size()
            viewport_x = viewport_size[0]
            viewport_y = viewport_size[1]
            viewport_aspect_ratio = viewport_x/viewport_y

            # 获取相机的POV信息
            view_target_pov_desc = game.unreal_service.find_property_by_name_on_object(uobject=player_camera_manager, property_name="ViewTarget.POV")
            view_target_pov = game.unreal_service.get_property_value(property_desc=view_target_pov_desc)

            # 计算调整后的FOV
            fov = view_target_pov["fOV"]*math.pi/180.0
            half_fov = fov/2.0
            half_fov_adjusted = math.atan(math.tan(half_fov)*viewport_aspect_ratio/view_target_pov["aspectRatio"])
            fov_adjusted = half_fov_adjusted*2.0
            fov_adjusted_degrees = fov_adjusted*180.0/math.pi

            # 存储当前相机pose (保持原始格式以便后续使用)
            camera_pose = {
                'index': i,
                'location': view_target_pov["location"],  # 保持原始字典格式
                'rotation': view_target_pov["rotation"],  # 保持原始字典格式
                'fov': fov_adjusted_degrees,
                'aspect_ratio': viewport_aspect_ratio,
                'viewport_x': viewport_x,
                'viewport_y': viewport_y
            }
            
            camera_poses_list.append(camera_pose)
            # spear.log(f"保存相机pose {i+1}/{num_camera_poses}: 位置({camera_pose['location']['X']:.2f}, {camera_pose['location']['Y']:.2f}, {camera_pose['location']['Z']:.2f})")
            
            i += 1

        with instance.end_frame():
            pass

    # 将poses保存到CSV文件，将嵌套字典序列化为JSON字符串
    import json
    serialized_poses = []
    for pose in camera_poses_list:
        serialized_pose = {
            'index': pose['index'],
            'location': json.dumps(pose['location']),  # 序列化为JSON字符串
            'rotation': json.dumps(pose['rotation']),  # 序列化为JSON字符串
            'fov': pose['fov'],
            'aspect_ratio': pose['aspect_ratio'], 
            'viewport_x': pose['viewport_x'],
            'viewport_y': pose['viewport_y']
        }
        serialized_poses.append(serialized_pose)
    
    df = pd.DataFrame(serialized_poses)
    camera_poses_file = os.path.realpath(os.path.join(camera_poses_dir, "camera_poses.csv"))
    df.to_csv(camera_poses_file, index=False)

    instance.close()

    spear.log(f"完成! 已保存 {num_camera_poses} 个相机poses到: {camera_poses_file}")
