import numpy as np
import json
import matplotlib.pyplot as plt
from mpl_toolkits.mplot3d import Axes3D
import open3d as o3d

def load_camera_data(json_file):
    """加载相机JSON文件并提取外参矩阵"""
    with open(json_file, 'r') as f:
        data = json.load(f)
    
    extrinsic_list = data['extrinsic_cam2world']
    extrinsic_3x4 = np.array(extrinsic_list).reshape((3, 4))
    
    # 构建4x4齐次坐标矩阵
    cam2world = np.eye(4)
    cam2world[:3, :4] = extrinsic_3x4
    
    return cam2world

def depth_to_pointcloud(depth, cam2world, fov_v, screen_width, screen_height):
    """将深度图转换为点云"""
    # 计算内参矩阵参数
    fov_h = 2 * np.arctan(np.tan(np.radians(fov_v)/2) * (screen_width/screen_height))
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

def apply_transform(points, transform_matrix):
    """应用变换矩阵到点云"""
    points_homo = np.hstack([points, np.ones((points.shape[0], 1))])
    points_transformed_homo = np.dot(points_homo, transform_matrix.T)
    return points_transformed_homo[:, :3]

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
    
    # 渲染并保持窗口打开
    vis.run()
    vis.destroy_window()

def test_transforms(gtav_points, cp_points, transforms):
    """测试不同的变换矩阵"""
    best_transform = None
    best_score = float('inf')
    
    for i, (name, transform) in enumerate(transforms.items()):
        print(f"测试变换: {name}")
        
        # 应用变换
        transformed_points = apply_transform(gtav_points, transform)
        
        # 计算与赛博朋克点云的差异（简单的中心距离）
        gtav_center = np.mean(transformed_points, axis=0)
        cp_center = np.mean(cp_points, axis=0)
        distance = np.linalg.norm(gtav_center - cp_center)
        
        print(f"  中心距离: {distance}")
        
        # 可视化
        visualize_pointcloud(transformed_points, f"GTAV - {name}")
        
        # 更新最佳变换
        if distance < best_score:
            best_score = distance
            best_transform = (name, transform)
    
    return best_transform

def main():
    # 文件路径
    gtav_file = "F:\\SteamLibrary\\steamapps\\common\\Grand Theft Auto V\\cv_saved\\GTAV_2025-08-30_2057837496_meta.json"
    cp_file = "E:\\steam\\steamapps\\common\\Cyberpunk 2077\\bin\\x64\\cv_saved\\actions_2025-08-28_288796023\\frame_000000_camera.json"
    
    # 加载相机数据
    gtav_cam2world = load_camera_data(gtav_file)
    cp_cam2world = load_camera_data(cp_file)
    
    # 打印相机信息
    print("GTAV相机位置:", gtav_cam2world[:3, 3])
    print("赛博朋克2077相机位置:", cp_cam2world[:3, 3])
    
    # 创建测试深度图（假设所有点距离相机5米）
    screen_width, screen_height = 1920, 1080  # 假设的分辨率
    depth = np.full((screen_height, screen_width), 5.0, dtype=np.float32)
    
    # 生成点云
    gtav_points = depth_to_pointcloud(depth, gtav_cam2world, 50.0, screen_width, screen_height)
    cp_points = depth_to_pointcloud(depth, cp_cam2world, 69.0, screen_width, screen_height)
    
    # 可视化原始点云
    visualize_pointcloud(gtav_points, "GTAV Original")
    visualize_pointcloud(cp_points, "Cyberpunk 2077 Original")
    
    # 定义要测试的变换矩阵
    transforms = {
        "Identity": np.eye(4),
        "Swap Y-Z": np.array([[1, 0, 0, 0],
                             [0, 0, 1, 0],
                             [0, 1, 0, 0],
                             [0, 0, 0, 1]]),
        "Swap X-Y": np.array([[0, 1, 0, 0],
                             [1, 0, 0, 0],
                             [0, 0, 1, 0],
                             [0, 0, 0, 1]]),
        "Swap X-Z": np.array([[0, 0, 1, 0],
                             [0, 1, 0, 0],
                             [1, 0, 0, 0],
                             [0, 0, 0, 1]]),
        "Invert X": np.array([[-1, 0, 0, 0],
                             [0, 1, 0, 0],
                             [0, 0, 1, 0],
                             [0, 0, 0, 1]]),
        "Invert Y": np.array([[1, 0, 0, 0],
                             [0, -1, 0, 0],
                             [0, 0, 1, 0],
                             [0, 0, 0, 1]]),
        "Invert Z": np.array([[1, 0, 0, 0],
                             [0, 1, 0, 0],
                             [0, 0, -1, 0],
                             [0, 0, 0, 1]]),
        "Rotate X 90": np.array([[1, 0, 0, 0],
                                [0, 0, -1, 0],
                                [0, 1, 0, 0],
                                [0, 0, 0, 1]]),
        "Rotate Y 90": np.array([[0, 0, 1, 0],
                                [0, 1, 0, 0],
                                [-1, 0, 0, 0],
                                [0, 0, 0, 1]]),
        "Rotate Z 90": np.array([[0, -1, 0, 0],
                                [1, 0, 0, 0],
                                [0, 0, 1, 0],
                                [0, 0, 0, 1]]),
        "Swap Y-Z + Invert Y": np.array([[1, 0, 0, 0],
                                        [0, 0, -1, 0],
                                        [0, 1, 0, 0],
                                        [0, 0, 0, 1]]),
        "Swap Y-Z + Invert Z": np.array([[1, 0, 0, 0],
                                        [0, 0, 1, 0],
                                        [0, -1, 0, 0],
                                        [0, 0, 0, 1]]),
    }
    
    # 测试所有变换
    best_transform = test_transforms(gtav_points, cp_points, transforms)
    
    print(f"\n最佳变换: {best_transform[0]}")
    print("变换矩阵:")
    print(best_transform[1])
    
    # 应用最佳变换并可视化结果
    transformed_points = apply_transform(gtav_points, best_transform[1])
    visualize_pointcloud(transformed_points, f"GTAV - {best_transform[0]} (Best)")
    visualize_pointcloud(cp_points, "Cyberpunk 2077 (Reference)")

if __name__ == "__main__":
    main()