import nerfvis
import json
import numpy as np
import os
from PIL import Image

data_path = "transforms.json"
with open(data_path, 'r') as f:
    data = json.load(f)

scene = nerfvis.Scene("我的 NeRF 相机")

camera_angle_x = data.get('camera_angle_x', None)
camera_angle_y = data.get('camera_angle_y', None)

for i, frame in enumerate(data['frames']):
    transform_matrix = np.array(frame['transform_matrix'])
    image_path = frame['file_path']
    if not os.path.isabs(image_path):
        image_path = os.path.join(os.path.dirname(data_path), image_path)
    # 读取图片为 numpy 数组
    if os.path.exists(image_path):
        image = np.array(Image.open(image_path))
    else:
        image = None
    scene.add_camera_frustum(
        f"相机{i}",
        transform_matrix,
        fov_x=camera_angle_x,
        fov_y=camera_angle_y,
        image=image
    )

scene.display()