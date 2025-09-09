import cv2, numpy as np

seg = cv2.imread("E:\steam\steamapps\common\The Witcher 3\bin\x64\cv_saved\Witcher3_2025-08-23_89826810_semseg.png")
glass_color = (219, 46, 155)  # 注意OpenCV是BGR顺序
mask = cv2.inRange(seg, glass_color, glass_color)
cv2.imwrite("glass_mask.png", mask)
