import numpy as np

# 两帧的extrinsic_cam2world，分别替换为你的数据
extrinsic1 = [
    -0.6707795262336731, -0.7156810760498047, 0.19456462562084198, -11.364090919494629,
    0.7416420578956604, -0.6489254832267761, 0.16989019513130188, -1472.985107421875,
    0.004670755472034216, 0.2582561671733856, 0.9660651683807373, 31.21001625061035
]
extrinsic2 = [
    -0.6707795262336731, -0.7156810760498047, 0.19456462562084198, -11.364090919494629,
    0.7416420578956604, -0.6489254832267761, 0.16989019513130188, -1200,  # 只改y轴平移
    0.004670755472034216, 0.2582561671733856, 0.9660651683807373, 31.21001625061035
]

def extrinsic_to_matrix(extrinsic):
    mat = np.array(extrinsic, dtype=np.float64).reshape(3, 4)
    mat = np.vstack([mat, [0, 0, 0, 1]])
    return mat

# 构造一组虚拟点云（比如一组正方体点）
N = 1000
np.random.seed(42)
points = np.random.uniform(-10, 10, (N, 3))
points_h = np.hstack([points, np.ones((N, 1))]).T  # 4xN

# 分别用两帧的外参变换到世界坐标
mat1 = extrinsic_to_matrix(extrinsic1)
mat2 = extrinsic_to_matrix(extrinsic2)

world_points1 = (mat1 @ points_h).T[:, :3]
world_points2 = (mat2 @ points_h).T[:, :3]

print("第一帧点云均值(x, y, z):", world_points1.mean(axis=0))
print("第二帧点云均值(x, y, z):", world_points2.mean(axis=0))
print("两帧均值差:", world_points2.mean(axis=0) - world_points1.mean(axis=0))