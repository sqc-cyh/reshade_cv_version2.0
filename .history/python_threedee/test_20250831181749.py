import numpy as np

depth = np.load(r"E:\steam\steamapps\common\Cyberpunk 2077\bin\x64\cv_saved\Cyberpunk2077_2025-08-31_123948282_depth.npy")


H, W = depth.shape
center = depth[H//2, W//2]
edge_left = depth[H//2, 0]
edge_right = depth[H//2, -1]

print("Center pixel depth:", center)
print("Left edge depth:", edge_left)
print("Right edge depth:", edge_right)

ratio_left = edge_left / center
ratio_right = edge_right / center

print("Edge/Center ratios:", ratio_left, ratio_right)
