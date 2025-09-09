import numpy as np
from scipy.spatial.transform import Rotation as R

mat = np.array([
    [0.72638637, 0.07990137, 0.68262631],
    [0.68727374, -0.09053582, -0.72073448],
    [0.00421445, 0.99268281, -0.12067809]
])

# ZYX顺序，degrees=True
rot = R.from_matrix(mat)
euler = rot.as_euler('ZYX', degrees=True)
print("Rotation.Z (Yaw):", euler[0])
print("Rotation.Y (Roll):", euler[1])
print("Rotation.X (Pitch):", euler[2])