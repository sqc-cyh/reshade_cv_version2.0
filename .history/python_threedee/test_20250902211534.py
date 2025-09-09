import nerfvis
import json
import numpy as np
import trimesh

# 你的 transforms.json 文件路径
data_path = "transforms.json"

# 加载 transforms.json 文件
with open(data_path, 'r') as f:
    data = json.load(f)

# 创建一个 NerfVis 场景
scene = nerfvis.Scene("我的 NeRF 相机和点云")

# 获取相机内参
fl_x = data['fl_x']
fl_y = data['fl_y']
w = data['w']
h = data['h']

# 遍历每一帧并添加相机
for i, frame in enumerate(data['frames']):
    # 获取相机姿态矩阵 (camera-to-world)
    transform_matrix = np.array(frame['transform_matrix'])
    
    # 提取旋转矩阵（R）和平移向量（t）
    R = transform_matrix[:3, :3]
    t = transform_matrix[:3, 3]
    
    # 获取图像的相对路径
    image_path = frame['file_path']

    # 使用 NerfVis 添加相机视锥
    scene.add_camera_frustum(
        name=f"相机{i}",
        r=R,
        t=t,
        focal_length_x=fl_x,
        focal_length_y=fl_y,
        image_width=w,
        image_height=h,
        image_path=image_path
        scale=5.0
    )

# 加载并显示点云文件
# 请将文件路径替换为你的实际路径
ply_path = "F:/SteamLibrary/steamapps/common/Grand Theft Auto V/cv_saved/merged_sparse.ply"
mesh = trimesh.load(ply_path)

# 将点云添加到场景中
scene.add_points("点云", mesh.vertices, colors=mesh.colors)

# 启动 NerfVis 服务器并显示场景
scene.display()