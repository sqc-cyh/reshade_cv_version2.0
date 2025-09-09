import cv2
import numpy as np
from pathlib import Path

# 正确处理路径：用 r"" 原始字符串 或者正斜杠
seg_path = Path(r"E:\steam\steamapps\common\The Witcher 3\bin\x64\cv_saved\Witcher3_2025-08-23_89826810_semseg.png")
out_path = Path(r"E:\steam\steamapps\common\The Witcher 3\bin\x64\cv_saved\glass_mask.png")

# 读入分割图
seg = cv2.imread(str(seg_path), cv2.IMREAD_COLOR)
if seg is None:
    raise FileNotFoundError(f"无法读取图像: {seg_path}")

# 玻璃颜色 (RGB=#9b2edb -> BGR=(219,46,155))
glass_bgr = np.array([219, 46, 155], dtype=np.uint8)

# 可以设置容差，避免因为压缩/渲染差异导致完全匹配不到
tol = 3
lower = np.clip(glass_bgr - tol, 0, 255)
upper = np.clip(glass_bgr + tol, 0, 255)

# 生成 mask
mask = cv2.inRange(seg, lower, upper)

# 保存
cv2.imwrite(str(out_path), mask)
print(f"[OK] 已保存玻璃mask -> {out_path}")
