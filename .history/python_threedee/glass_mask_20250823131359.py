import cv2, numpy as np

seg = cv2.imread("Witcher3_2025-08-23_119035831_semseg.png")
glass_color = (219, 46, 155)  # 注意OpenCV是BGR顺序
mask = cv2.inRange(seg, glass_color, glass_color)
cv2.imwrite("glass_mask.png", mask)
