import numpy as np
import json
import matplotlib.pyplot as plt
from mpl_toolkits.mplot3d import Axes3D

def load_extrinsic(path):
    with open(path, 'r') as f:
        meta = json.load(f)
    ext = np.array(meta['extrinsic_cam2world']).reshape(3, 4)
    R = ext[:, :3]
    t = ext[:, 3]
    return R, t

def create_cube(center, size):
    # 生成正方体8个顶点
    d = size / 2.0
    points = np.array([[x, y, z] for x in [-d, d] for y in [-d, d] for z in [-d, d]])
    return points + center

def transform_points(points, R, t):
    return (R @ points.T).T + t

# 路径
meta1 = r'F:/SteamLibrary/steamapps/common/Grand Theft Auto V/cv_saved/GTAV_2025-08-30_3041100153_meta.json'
meta2 = r'F:/SteamLibrary/steamapps/common/Grand Theft Auto V/cv_saved/GTAV_2025-08-30_3043661146_meta.json'

# 加载相机参数
R1, t1 = load_extrinsic(meta1)
R2, t2 = load_extrinsic(meta2)

# 构建两个正方体
cube1 = create_cube(center=np.array([0, 0, 0]), size=10)
cube2 = create_cube(center=np.array([30, 0, 0]), size=10)

# 变换到相机坐标系
cube1_cam1 = transform_points(cube1, R1, t1)
cube2_cam2 = transform_points(cube2, R2, t2)

# 可视化
fig = plt.figure()
ax = fig.add_subplot(111, projection='3d')
ax.scatter(*cube1_cam1.T, c='r', label='Cube1 in Cam1')
ax.scatter(*cube2_cam2.T, c='b', label='Cube2 in Cam2')
ax.set_xlabel('X')
ax.set_ylabel('Y')
ax.set_zlabel('Z')
ax.legend()
plt.show()