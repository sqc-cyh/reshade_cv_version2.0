import cv2
import numpy as np
from pathlib import Path

# === 路径 ===
seg_path = Path(r"E:\steam\steamapps\common\The Witcher 3\bin\x64\cv_saved\Witcher3_2025-08-23_89826810_semseg.png")
rgb_path = Path(r"E:\steam\steamapps\common\The Witcher 3\bin\x64\cv_saved\Witcher3_2025-08-23_89826810_RGB.png")
out_path = Path(r"E:\steam\steamapps\common\The Witcher 3\bin\x64\cv_saved\Witcher3_2025-08-23_89826810_RGB_no_glass.png")

# === 读取图像 ===
seg = cv2.imread(str(seg_path), cv2.IMREAD_COLOR)
rgb = cv2.imread(str(rgb_path), cv2.IMREAD_COLOR)
if seg is None: raise FileNotFoundError(f"无法读取分割图: {seg_path}")
if rgb is None: raise FileNotFoundError(f"无法读取RGB图: {rgb_path}")
if seg.shape[:2] != rgb.shape[:2]:
    raise ValueError(f"尺寸不一致: seg={seg.shape} rgb={rgb.shape}")

# === 玻璃颜色（#9b2edb -> BGR=(219,46,155)）+ 容差 ===
glass_bgr = np.array([219, 46, 155], dtype=np.uint8)
tol = 3
lower = np.clip(glass_bgr - tol, 0, 255)
upper = np.clip(glass_bgr + tol, 0, 255)

# === 生成掩膜 ===
mask = cv2.inRange(seg, lower, upper)
mask = mask.astype(np.uint8)
k    = cv2.getStructuringElement(cv2.MORPH_ELLIPSE, (3,3))
mask = cv2.morphologyEx(mask, cv2.MORPH_CLOSE, k, iterations=1)
mask = cv2.GaussianBlur(mask, (3,3), 0)

# === 直接“去玻璃” ===
transparent_output = True  # True: 玻璃处透明；False: 玻璃处置黑

if transparent_output:
    bgra = cv2.cvtColor(rgb, cv2.COLOR_BGR2BGRA)
    bgra[mask > 0, 3] = 0             # A=0 → 透明
    cv2.imwrite(str(out_path), bgra)
else:
    out = rgb.copy()
    out[mask > 0] = (0, 0, 0)         # 置黑
    cv2.imwrite(str(out_path), out)

print(f"[OK] 已输出: {out_path}")
