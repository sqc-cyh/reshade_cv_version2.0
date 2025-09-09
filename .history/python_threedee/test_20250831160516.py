import numpy as np
import matplotlib.pyplot as plt
from mpl_toolkits.mplot3d import Axes3D

# 读取 .npy 文件
Z = np.load("data.npy")  # shape (H, W)

H, W = Z.shape
X, Y = np.meshgrid(np.arange(W), np.arange(H))

fig = plt.figure()
ax = fig.add_subplot(111, projection='3d')
ax.plot_surface(X, Y, Z, cmap='viridis')

plt.show()
