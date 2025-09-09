#!/usr/bin/env python3
# -*- coding: utf-8 -*-

import os
import json
import math
import argparse
from typing import Dict, Any, List, Tuple, Optional

import numpy as np
import cv2

try:
    import open3d as o3d
except Exception as e:
    o3d = None


def load_cam_jsonl(path: str) -> List[Dict[str, Any]]:
    cams = []
    with open(path, "r", encoding="utf-8") as f:
        for line in f:
            s = line.strip()
            if not s:
                continue
            cams.append(json.loads(s))
    return cams


def fov_to_fx_fy(
    w: int, h: int,
    fov_v_deg: Optional[float],
    fov_h_deg: Optional[float]
) -> Tuple[float, float]:
    """
    从垂直/水平 FoV 计算像素焦距 fx, fy.
    若仅提供一个方向的 FoV, 另一个按画幅比例换算.
    """
    aspect = (w / h) if h > 0 else 1.0
    fov_v = None
    fov_h = None
    if fov_v_deg is not None:
        fov_v = math.radians(float(fov_v_deg))
        fov_h = 2.0 * math.atan(math.tan(fov_v / 2.0) * aspect)
    elif fov_h_deg is not None:
        fov_h = math.radians(float(fov_h_deg))
        fov_v = 2.0 * math.atan(math.tan(fov_h / 2.0) / aspect)
    else:
        raise ValueError("Neither fov_v_degrees nor fov_h_degrees found.")

    fx = w / (2.0 * math.tan(fov_h / 2.0))
    fy = h / (2.0 * math.tan(fov_v / 2.0))
    return fx, fy


def parse_intrinsics(camj: Dict[str, Any], w: int, h: int) -> Tuple[float, float, float, float]:
    """
    解析单帧相机内参. 优先使用 fx,fy,cx,cy; 否则从 FoV 换算.
    支持以下字段名（若你的 json 字段不同, 可在这里扩展）:
        fx, fy, cx, cy
        fov_v_degrees, fov_h_degrees
    """
    cx = float(camj.get("cx", w * 0.5))
    cy = float(camj.get("cy", h * 0.5))
    if all(k in camj for k in ("fx", "fy")):
        fx = float(camj["fx"])
        fy = float(camj["fy"])
        return fx, fy, cx, cy
    # fov-based
    fov_v = camj.get("fov_v_degrees", None)
    fov_h = camj.get("fov_h_degrees", None)
    fx, fy = fov_to_fx_fy(w, h, fov_v, fov_h)
    return fx, fy, cx, cy


def parse_extrinsic_cam2world(camj: Dict[str, Any]) -> np.ndarray:
    """
    解析 3x4 或 4x4 的 cam2world. 返回 4x4.
    字段名优先: extrinsic_cam2world.
    若是 3x4, 自动 pad 成 4x4.
    """
    if "extrinsic_cam2world" not in camj:
        raise ValueError("cam json missing 'extrinsic_cam2world'")
    M = np.asarray(camj["extrinsic_cam2world"], dtype=np.float64)
    M = M.reshape(-1)
    if M.size == 12:  # 3x4
        M = M.reshape(3, 4)
        P = np.eye(4, dtype=np.float64)
        P[:3, :4] = M
        return P
    elif M.size == 16:  # 4x4
        return M.reshape(4, 4)
    else:
        raise ValueError(f"Invalid extrinsic_cam2world size: {M.size}")


def depth_from_frame(
    depth_frame_bgr: np.ndarray,
    encoding: str,
    scale: float,
    near: Optional[float],
    far: Optional[float]
) -> np.ndarray:
    """
    将 depth.mp4 的一帧解码为 float32 的“米”单位深度图。
    - encoding = 'u8'  : 输入为 8-bit（灰度在 BGR 的任一通道），用 scale 将 [0,255] 映射到米；若 near/far 给出，则线性反解到 [near,far].
    - encoding = 'u16' : 读取 16-bit（cv2 VideoCapture 通常会给到 8bit；若你用自定义编解码得到 16bit，需要自行读取或从单独文件来）
    - encoding = 'f32' : 读取 32-bit float（同样注意编解码链路是否能保真）
    说明：多数情况下 depth.mp4 实际是 8-bit tone-mapped 预览，这时推荐用 --depth-encoding u8 并给一个合理的 --depth-scale 或 near/far。
    """
    if encoding.lower() == "u8":
        if depth_frame_bgr.ndim == 3:
            gray = cv2.cvtColor(depth_frame_bgr, cv2.COLOR_BGR2GRAY)
        else:
            gray = depth_frame_bgr
        gray = gray.astype(np.float32)  # 0..255
        if near is not None and far is not None and far > near:
            # 将 0..255 线性映射回 [near, far]
            d = near + (gray / 255.0) * (far - near)
        else:
            # 简单线性缩放到“米”
            d = gray * float(scale)
        return d

    elif encoding.lower() == "u16":
        # 注：标准的 mp4 难以无损承载 16-bit 深度，除非自定义编解码。一般请用独立的 .png/.npy 存储。
        # 这里保留接口，若你实际能以 16bit 读入，就把 BGR 拆成 16bit。
        if depth_frame_bgr.dtype != np.uint16:
            raise ValueError("depth frame is not uint16; check your pipeline or use --depth-encoding u8")
        d = depth_frame_bgr.astype(np.float32) * float(scale)
        return d

    elif encoding.lower() == "f32":
        if depth_frame_bgr.dtype != np.float32:
            raise ValueError("depth frame is not float32; check your pipeline")
        d = depth_frame_bgr.copy()
        if scale is not None:
            d *= float(scale)
        return d

    else:
        raise ValueError(f"Unsupported depth encoding: {encoding}")


def backproject_points(
    depth: np.ndarray,
    rgb: Optional[np.ndarray],
    fx: float, fy: float, cx: float, cy: float,
    cam2world: np.ndarray,
    every_n: int = 1,
    min_depth: float = 0.0,
    max_depth: float = 1e9
) -> Tuple[np.ndarray, Optional[np.ndarray]]:
    """
    将一帧深度反投影到世界坐标系，返回 Nx3 (points) 与 Nx3 (colors).
    every_n: 对像素做步进抽样以控制点量。
    """
    H, W = depth.shape[:2]
    # 像素网格（下采样）
    vs = np.arange(0, H, every_n, dtype=np.int32)
    us = np.arange(0, W, every_n, dtype=np.int32)
    uu, vv = np.meshgrid(us, vs)  # shape: [H', W']

    d = depth[vv, uu].astype(np.float32)  # [H', W']
    mask = (d > min_depth) & (d < max_depth) & np.isfinite(d)
    if not np.any(mask):
        return np.zeros((0, 3), np.float32), (None if rgb is None else np.zeros((0, 3), np.float32))

    uu = uu[mask].astype(np.float32)
    vv = vv[mask].astype(np.float32)
    dd = d[mask]  # 米

    x = (uu - cx) * dd / fx
    y = (vv - cy) * dd / fy
    z = dd

    # 相机坐标 -> 齐次
    cam_pts = np.stack([x, y, z, np.ones_like(z)], axis=0)  # [4, N]
    world_pts = (cam2world @ cam_pts)  # [4, N]
    world_pts = world_pts[:3, :].T.astype(np.float32)       # [N, 3]

    colors = None
    if rgb is not None:
        # OpenCV 读进来是 BGR，这里转 RGB
        if rgb.ndim == 3 and rgb.shape[2] == 3:
            col = rgb[vv.astype(np.int32), uu.astype(np.int32), :]  # [N, 3], BGR
            col = col[:, ::-1]  # RGB
            if col.dtype == np.uint8:
                col = (col.astype(np.float32) / 255.0)
            colors = col.astype(np.float32)

    return world_pts, colors


def main():
    parser = argparse.ArgumentParser(description="Fuse RGB/Depth videos with per-frame camera to build a global point cloud (Open3D-viewable).")
    parser.add_argument("--rgb", type=str, required=True, help="Path to capture.mp4 (RGB).")
    parser.add_argument("--depth", type=str, required=True, help="Path to depth.mp4 (Depth).")
    parser.add_argument("--cam-jsonl", type=str, required=True, help="Path to cam.jsonl (one JSON per line).")
    parser.add_argument("--out", type=str, default="cloud.ply", help="Output PLY path.")
    parser.add_argument("--every-n", type=int, default=2, help="Pixel subsample stride (>=1).")
    parser.add_argument("--min-depth", type=float, default=0.0, help="Ignore depths < this (meters).")
    parser.add_argument("--max-depth", type=float, default=1e9, help="Ignore depths > this (meters).")
    parser.add_argument("--depth-encoding", type=str, default="u8", choices=["u8", "u16", "f32"], help="Depth video encoding assumption.")
    parser.add_argument("--depth-scale", type=float, default=0.01, help="Scale to convert raw depth units to meters (used for u8/u16; for u8 it's meters per 255).")
    parser.add_argument("--near", type=float, default=None, help="If provided with --far, map u8 [0..255] linearly to [near, far] meters.")
    parser.add_argument("--far", type=float, default=None, help="See --near.")
    parser.add_argument("--max-frames", type=int, default=-1, help="Limit frames for quick test; -1 = all.")
    parser.add_argument("--voxel", type=float, default=0.0, help="Open3D voxel size for downsampling in meters (0=off).")
    args = parser.parse_args()

    cams = load_cam_jsonl(args.cam_jsonl)
    if len(cams) == 0:
        raise RuntimeError("Empty cam.jsonl")

    cap_rgb = cv2.VideoCapture(args.rgb)
    cap_dep = cv2.VideoCapture(args.depth)
    if not cap_rgb.isOpened():
        raise RuntimeError(f"Failed to open RGB video: {args.rgb}")
    if not cap_dep.isOpened():
        raise RuntimeError(f"Failed to open Depth video: {args.depth}")

    n_rgb = int(cap_rgb.get(cv2.CAP_PROP_FRAME_COUNT)) or 0
    n_dep = int(cap_dep.get(cv2.CAP_PROP_FRAME_COUNT)) or 0
    n_cam = len(cams)

    n = min(n_rgb, n_dep, n_cam)
    if args.max_frames > 0:
        n = min(n, args.max_frames)

    if n < 1:
        raise RuntimeError(f"Nothing to process. n_rgb={n_rgb}, n_dep={n_dep}, n_cam={n_cam}")

    if not (n_rgb == n_dep == n_cam):
        print(f"[Warn] Length mismatch: rgb={n_rgb}, depth={n_dep}, cam={n_cam}. Will use n={n} frames.")

    pts_all: List[np.ndarray] = []
    cols_all: List[np.ndarray] = []

    for i in range(n):
        ok_rgb, frame_rgb = cap_rgb.read()
        ok_dep, frame_dep = cap_dep.read()
        if not ok_rgb or not ok_dep:
            print(f"[Warn] Break at frame {i}: ok_rgb={ok_rgb}, ok_dep={ok_dep}")
            break

        Hc, Wc = frame_rgb.shape[:2]
        # 解析深度（转为米）
        depth = depth_from_frame(
            frame_dep, args.depth_encoding, args.depth_scale,
            args.near, args.far
        )
        Hd, Wd = depth.shape[:2]
        if (Hd != Hc) or (Wd != Wc):
            # 简单 resize 深度到 RGB 尺寸（注意可能引入插值；如介意可改为等比裁剪）
            depth = cv2.resize(depth, (Wc, Hc), interpolation=cv2.INTER_NEAREST)

        # 解析相机参数
        camj = cams[i]
        try:
            fx, fy, cx, cy = parse_intrinsics(camj, Wc, Hc)
        except Exception as e:
            raise RuntimeError(f"Frame {i}: intrinsics parse failed: {e}")

        try:
            cam2world = parse_extrinsic_cam2world(camj)
        except Exception as e:
            raise RuntimeError(f"Frame {i}: extrinsics parse failed: {e}")

        # 反投影
        pts, cols = backproject_points(
            depth=depth, rgb=frame_rgb,
            fx=fx, fy=fy, cx=cx, cy=cy,
            cam2world=cam2world,
            every_n=max(1, args.every_n),
            min_depth=args.min_depth, max_depth=args.max_depth
        )

        if pts.size == 0:
            continue

        pts_all.append(pts)
        if cols is not None:
            cols_all.append(cols)

        if (i + 1) % 10 == 0:
            print(f"Processed {i+1}/{n} frames, points so far: {sum(p.shape[0] for p in pts_all)}")

    cap_rgb.release()
    cap_dep.release()

    if len(pts_all) == 0:
        raise RuntimeError("No points reconstructed. Check depth encoding/scale and camera parameters.")

    pts_all = np.concatenate(pts_all, axis=0)
    cols_all = np.concatenate(cols_all, axis=0) if len(cols_all) == len(pts_all.shape) else (np.concatenate(cols_all, axis=0) if len(cols_all) > 0 else None)

    # 构建 Open3D 点云
    if o3d is None:
        raise RuntimeError("open3d is not installed. Please `pip install open3d`.")

    pcd = o3d.geometry.PointCloud()
    pcd.points = o3d.utility.Vector3dVector(pts_all.astype(np.float64))
    if cols_all is not None and cols_all.shape[0] == pts_all.shape[0]:
        pcd.colors = o3d.utility.Vector3dVector(cols_all.astype(np.float64))

    # 可选体素下采样
    if args.voxel > 1e-9:
        pcd = pcd.voxel_down_sample(voxel_size=float(args.voxel))

    # 保存 & 可视化
    o3d.io.write_point_cloud(args.out, pcd)
    print(f"[OK] Saved point cloud to: {args.out} ({len(pcd.points)} points)")
    o3d.visualization.draw_geometries([pcd])


if __name__ == "__main__":
    main()
