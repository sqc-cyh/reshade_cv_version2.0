import numpy as np
import json
import open3d as o3d
from PIL import Image
from game_camera import build_intrinsicmatrix_camtoscreenpix_pinhole_camera
from save_point_cloud_to_file import save_cloud_to_file
from load_point_cloud import load_depth_and_camjson

# 加载深度图和相机参数
depthfile = 'F:/SteamLibrary/steamapps/common/Grand Theft Auto V/cv_saved/GTAV_2025-08-30_492686459_depth.npy'
depth, camjson, rgb = load_depth_and_camjson(depthfile, True)

# 获取相机内参
screen_width = depth.shape[1]
screen_height = depth.shape[0]
fov_v = float(camjson['fov_v_degrees'])  # 竖直FOV
cam2screen = build_intrinsicmatrix_camtoscreenpix_pinhole_camera(fov_v, screen_width, screen_height)

# 提取相机外参
extrinsic_cam2world = np.array(camjson['extrinsic_cam2world']).reshape(3, 4)

# 将深度图转换为3D点云
def depth_to_point_cloud(depth, cam2screen, extrinsic_cam2world):
    # 创建网格坐标
    height, width = depth.shape
    i, j = np.meshgrid(np.arange(width), np.arange(height))
    rays = np.stack([j, i, np.ones_like(i)], axis=-1).reshape(-1, 3)  # 像素坐标

    # 将像素坐标转换为相机坐标系下的光线
    rays = rays * depth.reshape(-1, 1)  # 使用深度值进行缩放
    rays_camera = np.dot(np.linalg.inv(cam2screen), rays.T).T  # 将像素坐标转换为相机坐标系

    # 使用外参将相机坐标系转换到世界坐标系
    points_world = np.dot(rays_camera, extrinsic_cam2world[:, :3].T) + extrinsic_cam2world[:, 3]
    return points_world

# 生成3D点云
pcd_points = depth_to_point_cloud(depth, cam2screen, extrinsic_cam2world)

# 可视化点云
pcd_o3d = o3d.geometry.PointCloud()
pcd_o3d.points = o3d.utility.Vector3dVector(pcd_points)

# 可选：保存点云到文件
save_cloud_to_file({'worldpoints': pcd_points}, 'output.pcd')

# 显示点云
o3d.visualization.draw_geometries([pcd_o3d])
