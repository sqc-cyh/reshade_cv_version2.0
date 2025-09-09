import numpy as np
import json
import open3d as o3d
import os

def load_camera_data(json_file):
    """加载相机JSON文件并提取外参矩阵"""
    with open(json_file, 'r') as f:
        data = json.load(f)
    
    extrinsic_list = data['extrinsic_cam2world']
    extrinsic_3x4 = np.array(extrinsic_list).reshape((3, 4))
    
    # 构建4x4齐次坐标矩阵
    cam2world = np.eye(4)
    cam2world[:3, :4] = extrinsic_3x4
    
    return cam2world, data.get('fov_v_degrees', 50.0)

def depth_to_pointcloud(depth, cam2world, fov_v, screen_width, screen_height):
    """将深度图转换为点云"""
    # 计算内参矩阵参数
    aspect_ratio = screen_width / screen_height
    fov_h = 2 * np.arctan(np.tan(np.radians(fov_v)/2) * aspect_ratio)
    fx = screen_width / (2 * np.tan(fov_h/2))
    fy = screen_height / (2 * np.tan(np.radians(fov_v)/2))
    cx = screen_width / 2
    cy = screen_height / 2
    
    # 生成像素坐标网格
    u, v = np.meshgrid(np.arange(screen_width), np.arange(screen_height))
    u = u.astype(np.float32)
    v = v.astype(np.float32)
    
    # 将深度图展平
    z = depth.flatten()
    
    # 跳过无效的深度值
    valid_mask = (z > 0) & (z < float('inf'))
    z = z[valid_mask]
    u = u.flatten()[valid_mask]
    v = v.flatten()[valid_mask]
    
    # 计算相机坐标系下的点
    x = (u - cx) * z / fx
    y = (v - cy) * z / fy
    
    # 组合成相机坐标系下的点云 (N×3)
    points_cam = np.stack([x, y, z], axis=-1)
    
    # 转换为齐次坐标 (N×4)
    points_cam_homo = np.hstack([points_cam, np.ones((points_cam.shape[0], 1))])
    
    # 变换到世界坐标系
    points_world_homo = np.dot(points_cam_homo, cam2world.T)
    points_world = points_world_homo[:, :3]
    
    return points_world

def visualize_pointcloud(points, title="Point Cloud"):
    """使用Open3D可视化点云"""
    pcd = o3d.geometry.PointCloud()
    pcd.points = o3d.utility.Vector3dVector(points)
    
    # 计算点云中心并设置视角
    center = np.mean(points, axis=0)
    
    # 设置可视化选项
    vis = o3d.visualization.Visualizer()
    vis.create_window(window_name=title, width=800, height=600)
    vis.add_geometry(pcd)
    
    # 设置视角
    view_control = vis.get_view_control()
    view_control.set_lookat(center)
    view_control.set_front([0, 0, -1])  # 从前方看
    view_control.set_up([0, 1, 0])      # Y轴向上
    
    # 渲染并保持窗口开放
    vis.run()
    vis.destroy_window()

def analyze_gtav_frames(gtav_files):
    """分析GTAV多帧数据，找出错位原因"""
    all_points = []
    camera_positions = []
    
    for i, file_path in enumerate(gtav_files):
        # 加载相机数据
        cam2world, fov_v = load_camera_data(file_path)
        camera_positions.append(cam2world[:3, 3])
        
        # 创建测试深度图
        screen_width, screen_height = 1920, 1080
        depth = np.full((screen_height, screen_width), 5.0, dtype=np.float32)
        
        # 生成点云
        points = depth_to_pointcloud(depth, cam2world, fov_v, screen_width, screen_height)
        all_points.append(points)
        
        print(f"帧 {i}: 相机位置 = {cam2world[:3, 3]}")
    
    # 可视化所有点云
    combined_points = np.concatenate(all_points, axis=0)
    visualize_pointcloud(combined_points, "All GTAV Frames")
    
    # 可视化相机轨迹
    camera_positions = np.array(camera_positions)
    visualize_pointcloud(camera_positions, "Camera Positions")
    
    # 分析相机运动
    print("\n相机运动分析:")
    for i in range(1, len(camera_positions)):
        movement = camera_positions[i] - camera_positions[i-1]
        distance = np.linalg.norm(movement)
        print(f"帧 {i-1} -> 帧 {i}: 移动向量 = {movement}, 距离 = {distance}")
    
    return all_points, camera_positions

def main():
    # GTAV文件列表（请替换为实际文件路径）
    gtav_files = [
        r"F:\SteamLibrary\steamapps\common\Grand Theft Auto V\cv_saved\GTAV_2025-08-30_599541369_meta.json",
        r"F:\SteamLibrary\steamapps\common\Grand Theft Auto V\cv_saved\GTAV_2025-08-30_98293461_meta.json"
    ]
    
    # 检查文件是否存在
    valid_files = []
    for file_path in gtav_files:
        if os.path.exists(file_path):
            valid_files.append(file_path)
        else:
            print(f"文件不存在: {file_path}")
    
    if len(valid_files) < 2:
        print("需要至少两个有效的GTAV文件进行分析")
        return
    
    # 分析GTAV多帧数据
    all_points, camera_positions = analyze_gtav_frames(valid_files)
    
    # 检查第一帧和最后一帧的相机位置差异
    first_pos = camera_positions[0]
    last_pos = camera_positions[-1]
    total_movement = last_pos - first_pos
    total_distance = np.linalg.norm(total_movement)
    
    print(f"\n总体相机运动:")
    print(f"起始位置: {first_pos}")
    print(f"结束位置: {last_pos}")
    print(f"总移动向量: {total_movement}")
    print(f"总移动距离: {total_distance}")

if __name__ == "__main__":
    main()