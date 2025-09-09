import nerfvis
import json
import numpy as np
import os
from PIL import Image
import open3d as o3d

# 1. 加载点云
pcd = o3d.io.read_point_cloud("your_pointcloud.ply")  # 替换为你的点云文件路径
points = np.asarray(pcd.points)
if pcd.colors:
    colors = np.asarray(pcd.colors)
else:
    colors = np.ones_like(points)

# 2. 加载transforms.json
data_path = "transforms.json"
with open(data_path, 'r') as f:
    data = json.load(f)

scene = nerfvis.Scene("点云+相机可视化")

# 3. 添加点云
scene.add_point_cloud(points, colors)

# 4. 添加相机视锥
camera_angle_x = data.get('camera_angle_x', None)
camera_angle_y = data.get('camera_angle_y', None)

for i, frame in enumerate(data['frames']):
    transform_matrix = np.array(frame['transform_matrix'])
    image_path = frame['file_path']
    if not os.path.isabs(image_path):
        image_path = os.path.join(os.path.dirname(data_path), image_path)
    # 读取图片（可选）
    if os.path.exists(image_path):
        image = np.array(Image.open(image_path))
    else:
        image = None
    # 兼容不同nerfvis版本
    try:
        scene.add_camera_frustum(
            f"相机{i}",
            transform_matrix,
            fov_x=camera_angle_x,
            fov_y=camera_angle_y,
            image=image
        )
    except TypeError:
        scene.add_camera_frustum(
            f"相机{i}",
            transform_matrix
        )

scene.display()