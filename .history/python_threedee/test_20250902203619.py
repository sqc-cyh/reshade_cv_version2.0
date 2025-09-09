import nerfvis
import json
import numpy as np
import os

# 加载 transforms.json 文件
data_path = "transforms.json"
with open(data_path, 'r') as f:
    data = json.load(f)

# 创建 NerfVis 场景
scene = nerfvis.Scene("我的 NeRF 相机")

# 获取相机内参
camera_angle_x = data.get('camera_angle_x', None)
camera_angle_y = data.get('camera_angle_y', None)

# 遍历每一帧并添加相机
for i, frame in enumerate(data['frames']):
    transform_matrix = np.array(frame['transform_matrix'])
    image_path = frame['file_path']
    # 如果图片路径不是绝对路径，自动补全
    if not os.path.isabs(image_path):
        image_path = os.path.join(os.path.dirname(data_path), image_path)
    scene.add_camera_frustum(
        f"相机{i}",
        pose=transform_matrix,
        fov_x=camera_angle_x,
        fov_y=camera_angle_y,
        image_path=image_path
    )

# 启动 NerfVis 服务器并显示场景
scene.display()