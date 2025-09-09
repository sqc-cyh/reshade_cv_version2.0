import nerfvis
import json
import numpy as np

# 替换为你的 transforms.json 文件的实际路径
# 如果你的 Python 脚本和 transforms.json 在同一个文件夹，则只需写文件名
data_path = "transforms.json"

# 加载 transforms.json 文件
with open(data_path, 'r') as f:
    data = json.load(f)

# 创建一个 NerfVis 场景
scene = nerfvis.Scene("我的 NeRF 相机")

# 获取相机内参（所有帧都相同）
fl_x = data['fl_x']
fl_y = data['fl_y']
cx = data['cx']
cy = data['cy']
w = data['w']
h = data['h']
camera_angle_x = data['camera_angle_x']
camera_angle_y = data['camera_angle_y']


# 遍历每一帧并添加相机
for frame in data['frames']:
    # 获取相机姿态矩阵 (camera-to-world)
    transform_matrix = np.array(frame['transform_matrix'])
    
    # 获取图像路径
    image_path = frame['file_path']

    # 使用 NerfVis 添加相机视锥
    scene.add_camera_frustum(
        "相机/" + image_path,  # 给每个相机一个唯一的名称，方便在面板中区分
        pose=transform_matrix,
        fov_x=camera_angle_x,
        fov_y=camera_angle_y,
        image_path=image_path
    )

# 启动 NerfVis 服务器并显示场景
scene.display()