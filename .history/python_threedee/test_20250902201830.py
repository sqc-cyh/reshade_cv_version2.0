import nerfvis
import json
import numpy as np

# NerfVis 会在当前目录下寻找 transforms.json
# 所以如果你在 F:\Visual_Studio\Repos\tmp\python_threedee 目录下运行脚本，
# 只需要指定文件名即可
data_path = "transforms.json"

# 加载 transforms.json 文件
with open(data_path, 'r') as f:
    data = json.load(f)

# 创建一个 NerfVis 场景
scene = nerfvis.Scene("我的 NeRF 相机")

# 获取相机内参
camera_angle_x = data['camera_angle_x']
camera_angle_y = data['camera_angle_y']

# 遍历每一帧并添加相机
for i, frame in enumerate(data['frames']):
    # 获取相机姿态矩阵 (camera-to-world)
    transform_matrix = np.array(frame['transform_matrix'])

    # 获取图像的相对路径
    image_path = frame['file_path']

    # 使用 NerfVis 添加相机视锥
    # 为每个相机分配一个唯一的、清晰的名称
    scene.add_camera_frustum(
        f"相机{i}",
        pose=transform_matrix,
        fov_x=camera_angle_x,
        fov_y=camera_angle_y,
        image_path=image_path
    )

# 启动 NerfVis 服务器并显示场景
scene.display()