import nerfvis
import json
import numpy as np
import os

data_path = "transforms.json"
with open(data_path, 'r') as f:
    data = json.load(f)

scene = nerfvis.Scene("我的 NeRF 相机")

for i, frame in enumerate(data['frames']):
    transform_matrix = np.array(frame['transform_matrix'])
    scene.add_camera_frustum(
        f"相机{i}",
        transform_matrix
    )

scene.display()