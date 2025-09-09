import numpy as np
import open3d as o3d
import json
import matplotlib.pyplot as plt
from PIL import Image

depth1 = np.load('F:/SteamLibrary/steamapps/common/Grand Theft Auto V/cv_saved/GTAV_2025-08-30_492686459_depth.npy')
depth2 = np.load('F:/SteamLibrary/steamapps/common/Grand Theft Auto V/cv_saved/GTAV_2025-08-30_495057589_depth.npy')

rgb1 = np.array(Image.open('F:/SteamLibrary/steamapps/common/Grand Theft Auto V/cv_saved/GTAV_2025-08-30_492686459_RGB.png'))
rgb2 = np.array(Image.open('F:/SteamLibrary/steamapps/common/Grand Theft Auto V/cv_saved/GTAV_2025-08-30_495057589_RGB.png'))

with open('F:/SteamLibrary/steamapps/common/Grand Theft Auto V/cv_saved/GTAV_2025-08-30_492686459_meta.json', 'r') as f:
    meta1 = json.load(f)

with open('F:/SteamLibrary/steamapps/common/Grand Theft Auto V/cv_saved/GTAV_2025-08-30_495057589_meta.json', 'r') as f:
    meta2 = json.load(f)

# 提取相机外参和FOV
extrinsic1 = np.array(meta1['extrinsic_cam2world']).reshape(3, 4)
extrinsic2 = np.array(meta2['extrinsic_cam2world']).reshape(3, 4)
fov_v_degrees1 = meta1['fov_v_degrees']
fov_v_degrees2 = meta2['fov_v_degrees']

# 根据FOV生成相机内参（假设像素为正方形，使用简单针孔模型）
focal_length1 = 1 / np.tan(np.radians(fov_v_degrees1) / 2)
focal_length2 = 1 / np.tan(np.radians(fov_v_degrees2) / 2)
height, width = depth1.shape

intrinsics1 = np.array([[focal_length1, 0, width / 2],
                        [0, focal_length1, height / 2],
                        [0, 0, 1]])

intrinsics2 = np.array([[focal_length2, 0, width / 2],
                        [0, focal_length2, height / 2],
                        [0, 0, 1]])

# 将深度图投影到3D点云中
def depth_to_point_cloud(depth, intrinsics, extrinsics):
    # 从 extrinsics 中提取旋转矩阵和平移向量
    rotation_matrix = extrinsics[:, :3]
    translation_vector = extrinsics[:, 3]

    # 生成像素坐标
    height, width = depth.shape
    i, j = np.meshgrid(np.arange(width), np.arange(height))
    rays = np.stack([j, i, np.ones_like(i)], axis=-1).reshape(-1, 3)  # 像素坐标

    # 使用深度信息来归一化光线
    rays = rays * depth.reshape(-1, 1)  # (x, y, z)

    # 将像素坐标转换为相机坐标系
    rays_camera = np.dot(np.linalg.inv(intrinsics), rays.T).T  # 从像素坐标转换到相机坐标系

    # 将相机坐标系下的点转换到世界坐标系
    points_world = np.dot(rays_camera, rotation_matrix.T) + translation_vector
    return points_world


# 从深度数据获取点云
pcd1 = depth_to_point_cloud(depth1, intrinsics1, extrinsic1)
pcd2 = depth_to_point_cloud(depth2, intrinsics2, extrinsic2)

# 合并两帧的点云（如果需要，可以应用转换来对齐它们）
combined_pcd = np.vstack([pcd1, pcd2])

# 使用Open3D显示点云
pcd_o3d = o3d.geometry.PointCloud()
pcd_o3d.points = o3d.utility.Vector3dVector(combined_pcd)

# 显示点云
o3d.visualization.draw_geometries([pcd_o3d])
