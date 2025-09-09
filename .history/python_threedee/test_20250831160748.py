import numpy as np
import matplotlib.pyplot as plt

# 读取 .npy 文件
Z = np.load("matrix.npy")   # shape (H, W)

H, W = Z.shape
# 构造坐标网格
X, Y = np.meshgrid(np.arange(W), np.arange(H))

# 展平成一维
x = X.ravel()
y = Y.ravel()
z = Z.ravel()

# 绘制 3D 散点图
fig = plt.figure()
ax = fig.add_subplot(111, projection="3d")
ax.scatter(x, y, z, s=1, c=z, cmap="jet")  # s=点大小

ax.set_xlabel("X")
ax.set_ylabel("Y")
ax.set_zlabel("Z")
plt.show()
