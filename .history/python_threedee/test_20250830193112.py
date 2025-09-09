import numpy as np
import open3d as o3d
import json
import matplotlib.pyplot as plt
from PIL import Image

# 加载深度图数据
depth1 = np.load('/mnt/data/GTAV_2025-08-30_492686459_depth.npy')
depth2 = np.load('/mnt/data/GTAV_2025-08-30_495057589_depth.npy')

# 加载RGB图像
rgb1 = np.array(Image.open('/mnt/data/GTAV_2025-08-30_492686459_RGB.png'))
rgb2 = np.array(Image.open('/mnt/data/GTAV_2025-08-30_495057589_RGB.png'))

# 加载相机参数
with open('/mnt/data/GTAV_2025-08-30_492686459_meta.json', 'r') as f:
    meta1 = json.load(f)
with open('/mnt/data/GTAV_2025-08-30_495057589_meta.json', 'r') as f:
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
    height, width = depth.shape
    i, j = np.meshgrid(np.arange(width), np.arange(height))
    rays = np.stack([j, i, np.ones_like(i)], axis=-1).reshape(-1, 3)  # 像素坐标

    # 使用深度信息来归一化光线
    rays = rays * depth.reshape(-1, 1)  # (x, y, z)
    rays = np.hstack([rays, np.ones((rays.shape[0], 1))])  # 增加1用于齐次坐标

    # 应用外参矩阵（从相机坐标系到世界坐标系的转换）
    points_world = np.dot(np.linalg.inv(extrinsics), rays.T).T
    return points_world[:, :3]

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
