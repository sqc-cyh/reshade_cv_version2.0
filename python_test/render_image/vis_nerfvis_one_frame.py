# vis_nerfvis_one_frame.py
# 可视化：相机金字塔 + 纹理图像 + 彩色点云（深度为“点到相机中心欧氏距离”）
# 依赖：nerfvis, numpy, opencv-python

import os, glob, json, math, argparse
import numpy as np
import cv2
import nerfvis

# ---------- 小工具 ----------
def d2r(x): return x * math.pi / 180.0

def rot_x(rx):
    cr, sr = math.cos(rx), math.sin(rx)
    return np.array([[1, 0, 0],
                     [0, cr,-sr],
                     [0, sr, cr]], dtype=np.float64)

def rot_y(ry):
    cy, sy = math.cos(ry), math.sin(ry)
    return np.array([[ cy, 0, sy],
                     [  0, 1,  0],
                     [-sy, 0, cy]], dtype=np.float64)

def rot_z(rz):
    cz, sz = math.cos(rz), math.sin(rz)
    return np.array([[cz,-sz, 0],
                     [sz, cz, 0],
                     [ 0,  0, 1]], dtype=np.float64)

def ue_rotator_to_R_world(roll_deg, pitch_deg, yaw_deg):
    """
    UE Rotator: Roll (X), Pitch (Y), Yaw (Z)
    复合顺序：先 Roll，再 Pitch，再 Yaw（X->Y->Z）
    得到：将“相机局部轴(UE相机系)”变换到“UE世界系”的 3x3 旋转矩阵（C2W）
    """
    rx = rot_x(d2r(roll_deg))
    ry = rot_y(d2r(pitch_deg))
    rz = rot_z(d2r(yaw_deg))
    # 注意：右乘先应用；此处等价于 R = Rz * Ry * Rx
    return rz @ ry @ rx  # shape (3,3)

def make_K_from_fovx(fovx_deg, W, H, aspect_ratio=None):
    """
    输入水平FOV和分辨率 -> 计算 fx, fy, cx, cy
    若给定 aspect_ratio（W/H），用 hFOV -> vFOV 公式：tan(h/2)/tan(v/2) = ar
    参考：Epic论坛给出的关系式。ar = tan(hFOV/2)/tan(vFOV/2)  :contentReference[oaicite:4]{index=4}
    """
    if aspect_ratio is None:
        aspect_ratio = W / H
    fovx = d2r(fovx_deg)
    fx = (W * 0.5) / math.tan(fovx * 0.5)
    # vFOV
    v = 2.0 * math.atan(math.tan(fovx * 0.5) / aspect_ratio)
    fy = (H * 0.5) / math.tan(v * 0.5)
    cx = (W - 1) / 2.0
    cy = (H - 1) / 2.0
    return fx, fy, cx, cy

def pick_latest_file(pattern):
    files = glob.glob(pattern)
    if not files:
        return None
    files.sort(key=lambda p: os.path.getmtime(p))
    return files[-1]

def build_cv_c2w_from_ue(location, rotation, coord_hint="cv"):
    """
    将 UE 世界中的相机位姿（location xyz, rotation pitch/yaw/roll）
    转成 OpenCV 约定下的 C2W（3x3 R, 3 t）。
    - UE 世界系：左手(+X前、+Y右、+Z上)  :contentReference[oaicite:5]{index=5}
    - OpenCV 相机/世界系（这里统一设）：右手 (x右, y下, z前)
      用一个固定映射把 UE 轴映到 OpenCV 轴：
        x_cv =  y_ue
        y_cv = -z_ue
        z_cv =  x_ue
      因此 M = [[0,1,0],[0,0,-1],[1,0,0]]
    """
    M = np.array([[0, 1,  0],
                  [0, 0, -1],
                  [1, 0,  0]], dtype=np.float64)

    R_ue = ue_rotator_to_R_world(rotation['roll'], rotation['pitch'], rotation['yaw'])
    # 把“UE相机系->UE世界系”的旋转，映射到“CV相机系->CV世界系”：
    R_cv = M @ R_ue @ M.T

    t_ue = np.array([location['x'], location['y'], location['z']], dtype=np.float64)
    t_cv = M @ t_ue
    return R_cv, t_cv

def backproject_points_from_euclidean_depth(depth, fx, fy, cx, cy, stride=4):
    """
    depth[u,v] 是“点到相机中心的欧氏距离”(沿光线方向的距离)。
    对每个像素 (u,v):
      先构造光线方向 d_cam = [x, y, 1], 其中 x=(u-cx)/fx, y=(v-cy)/fy
      单位化 u_cam = d_cam / ||d_cam||
      则 3D 点 (相机系) = depth * u_cam
    """
    H, W = depth.shape[:2]
    us = np.arange(0, W, stride)
    vs = np.arange(0, H, stride)
    uu, vv = np.meshgrid(us, vs)
    x = (uu - cx) / fx
    y = (vv - cy) / fy
    ones = np.ones_like(x)
    dirs = np.stack([x, y, ones], axis=-1)  # (H/str, W/str, 3)
    norms = np.linalg.norm(dirs, axis=-1, keepdims=True) + 1e-8
    unit_dirs = dirs / norms

    d = depth[::stride, ::stride][..., None]  # (.,.,1)
    pts_cam = unit_dirs * d                  # (.,.,3)
    pts_cam = pts_cam.reshape(-1, 3)
    return pts_cam, uu.reshape(-1), vv.reshape(-1)

# ---------- 主流程 ----------
def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--output_dir", default="output", help="你保存RGB/Depth/Pose的目录")
    ap.add_argument("--prefix", default="render", help="文件前缀，如 render_rgb_*.png")
    ap.add_argument("--rgb", help="显式指定某个RGB文件（可选）")
    ap.add_argument("--depth", help="显式指定某个Depth .npy 文件（可选）")
    ap.add_argument("--pose_json", help="显式指定某个Pose .json 文件（可选）")
    ap.add_argument("--stride", type=int, default=4, help="点云下采样步长")
    ap.add_argument("--point_size", type=float, default=1.0)
    ap.add_argument("--z_size", type=float, default=0.3, help="nerfvis 相机金字塔尺寸 z")
    args = ap.parse_args()

    # 选择文件（若未显式指定，则取最新）
    rgb_path   = args.rgb or pick_latest_file(os.path.join(args.output_dir, f"{args.prefix}_rgb_*.png"))
    depth_path = args.depth or pick_latest_file(os.path.join(args.output_dir, f"{args.prefix}_depth_*.npy"))
    pose_path  = args.pose_json or pick_latest_file(os.path.join(args.output_dir, f"{args.prefix}_pose_*.json"))
    assert rgb_path and depth_path and pose_path, f"找不到文件，请检查目录与前缀：\nRGB={rgb_path}\nDepth={depth_path}\nPose={pose_path}"

    # 载入数据
    rgb_bgr = cv2.imread(rgb_path, cv2.IMREAD_UNCHANGED)
    assert rgb_bgr is not None, f"无法读取RGB: {rgb_path}"
    rgb = cv2.cvtColor(rgb_bgr, cv2.COLOR_BGR2RGB)
    H, W = rgb.shape[:2]

    depth = np.load(depth_path).astype(np.float64)
    if depth.ndim == 3:
        depth = depth[..., 0]
    assert depth.shape[0] == H and depth.shape[1] == W, "深度与RGB尺寸不一致"

    with open(pose_path, "r", encoding="utf-8") as f:
        pose = json.load(f)
    # 你的脚本里保存的是 fov_adjusted_degrees & aspect_ratio（视为“水平FOV”和“宽高比”）
    fovx_deg = float(pose["fov"])
    aspect_ratio = float(pose["aspect_ratio"])
    location = pose["location"]      # dict x,y,z
    rotation = pose["rotation"]      # dict pitch,yaw,roll（单位：度，UE Rotator）

    # 相机内参（像素焦距）
    fx, fy, cx, cy = make_K_from_fovx(fovx_deg, W, H, aspect_ratio)

    # UE -> OpenCV 的位姿（C2W）
    R_cv, t_cv = build_cv_c2w_from_ue(location, rotation)
    depth_mask = depth < np.percentile(depth, 80)
    # 用“欧氏距离深度”回投点云（在相机系）
    pts_cam, uu, vv = backproject_points_from_euclidean_depth(depth, fx, fy, cx, cy, stride=args.stride)
    # 相机系 -> 世界系（OpenCV约定）
    pts_world = (R_cv @ pts_cam.T).T + t_cv[None, :]

    # 点颜色（采样 RGB）
    colors = rgb[vv, uu, :].reshape(-1, 3) / 255.0
    colors = colors[depth_mask[vv, uu].reshape(-1)]
    pts_world = pts_world[depth_mask[vv, uu].reshape(-1)]
    scene = nerfvis.Scene("UE capture (RGB + Depth + Camera)", default_opencv=True)
    # 相机空间用 OpenCV（x右,y下,z前），世界同样设成 OpenCV 风格（y 朝下）
    scene.set_opencv()
    scene.set_opencv_world()
    pts_world = pts_world - pts_world.mean(axis=0)
    pts_world = pts_world *100
    # 相机金字塔 + 纹理平面（r/t 需要 C2W）
    scene.add_camera_frustum(
        "camera/frustum",
        r=R_cv, t=t_cv,
        focal_length=float(fx),
        image_width=W, image_height=H,
        z=float(args.z_size)
    )
    scene.add_image(
        "camera/image",
        rgb, r=R_cv, t=t_cv,
        focal_length=float(fx),
        z=float(args.z_size), image_size=min(1024, max(W, H))
    )

    # 点云
    scene.add_points("points/rgb", pts_world, point_size=args.point_size, vert_color=colors)

    # 坐标轴
    scene.add_axes()

    # 展示（会开本地端口，或用 export 保存）
    scene.display()  # 可改 scene.export("nerfvis_out") 写成静态网页

if __name__ == "__main__":
    main()

