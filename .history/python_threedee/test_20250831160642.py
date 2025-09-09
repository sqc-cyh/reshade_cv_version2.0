import numpy as np
import matplotlib.pyplot as plt

# 加载点云文件
points = np.load("F:\SteamLibrary\steamapps\common\Grand Theft Auto V\cv_saved\GTAV_2025-08-31_459287497_depth.npy")   # shape 应该是 (N, 3)

# 拆分 x, y, z
x, y, z = points[:, 0], points[:, 1], points[:, 2]

# 绘制 3D 散点图
fig = plt.figure()
ax = fig.add_subplot(111, projection="3d")

ax.scatter(x, y, z, s=1, c=z, cmap="jet")  # s=点大小, c=颜色映射

ax.set_xlabel("X")
ax.set_ylabel("Y")
ax.set_zlabel("Z")
plt.show()
