#!/usr/bin/env python3
# Copyright (C) 2022 Jason Bunk
# Modified for .npy + mp4 + per-frame camera.json format
import os
import argparse
import json
import numpy as np
import cv2
from game_camera import (
    vertical_fov_from_horizontal_fov_degrees,
    build_intrinsicmatrix_camtoscreenpix_pinhole_camera,
    depth_image_to_4dscreencolumnvectors,
    fovv_and_fovh_degrees_given_either,
)
from save_point_cloud_to_file import save_cloud_to_file
from functools import partial
from tqdm.contrib.concurrent import process_map


def fov_v_from_camjson(camjson: dict, screen_aspect_ratio_w_over_h: float):
    assert isinstance(camjson, dict), str(type(camjson))
    assert 'fov_v_degrees' in camjson or 'fov_h_degrees' in camjson, \
        f"Missing FoV in camera JSON. Keys: {sorted(list(camjson.keys()))}"
    if 'fov_v_degrees' in camjson:
        return float(camjson['fov_v_degrees'])
    return vertical_fov_from_horizontal_fov_degrees(float(camjson['fov_h_degrees']), screen_aspect_ratio_w_over_h)


def build_npy_frame_list(data_dir: str):
    """扫描所有 frame_XXXXXX_depth.npy 文件，返回有序帧索引列表"""
    npy_files = sorted(
        f for f in os.listdir(data_dir)
        if f.startswith("frame_") and f.endswith("_depth.npy")
    )
    if not npy_files:
        raise FileNotFoundError(f"No depth .npy files found in {data_dir}")

    frame_indices = []
    for f in npy_files:
        try:
            idx = int(f.split('_')[1])
            frame_indices.append(idx)
        except:
            continue

    print(f"✅ Found {len(frame_indices)} depth .npy files.")
    return sorted(frame_indices)


def load_frame_pointcloud(frame_idx: int, data_dir: str, video_path: str,
                          max_distance: float = None,
                          subsample_amt: int = 1,
                          fov_degrees_vertical: float = None,
                          fov_degrees_horizontal: float = None):
    """从 .npy、mp4、camera.json 加载单帧点云"""
    import numpy as np

    # --- 1. Load Depth from .npy ---
    depth_file = os.path.join(data_dir, f"frame_{frame_idx:06d}_depth.npy")
    if not os.path.isfile(depth_file):
        print(f"❌ Depth file not found: {depth_file}")
        return None

    try:
        depth = np.load(depth_file).astype(np.float32)
        if len(depth.shape) != 2:
            print(f"❌ Invalid depth shape: {depth.shape}")
            return None
        H, W = depth.shape
    except Exception as e:
        print(f"❌ Failed to load depth {depth_file}: {e}")
        return None

    # --- 2. Load RGB from video at correct frame and resolution ---
    cap = cv2.VideoCapture(video_path)
    total_video_frames = int(cap.get(cv2.CAP_PROP_FRAME_COUNT))
    if frame_idx >= total_video_frames:
        print(f"⚠️ Frame {frame_idx} exceeds video length ({total_video_frames})")
        cap.release()
        return None

    cap.set(cv2.CAP_PROP_POS_FRAMES, frame_idx)
    ret, bgr = cap.read()
    cap.release()
    if not ret:
        print(f"⚠️ Cannot read RGB frame {frame_idx}")
        return None

    rgb = cv2.cvtColor(bgr, cv2.COLOR_BGR2RGB)
    rgb_resized = cv2.resize(rgb, (W, H), interpolation=cv2.INTER_AREA)  # match depth resolution

    # --- 3. Load Camera JSON ---
    cam_file = os.path.join(data_dir, f"frame_{frame_idx:06d}_camera.json")
    if not os.path.isfile(cam_file):
        print(f"❌ Camera file not found: {cam_file}")
        return None
    try:
        with open(cam_file, 'r') as f:
            camjson = json.load(f)
        assert isinstance(camjson, dict), f"Invalid camera JSON type: {type(camjson)}"
        assert 'extrinsic_cam2world' in camjson, "Missing 'extrinsic_cam2world' in camera JSON"
    except Exception as e:
        print(f"❌ Failed to load camera json {cam_file}: {e}")
        return None

    # --- 4. Compute FOV ---
    aspect_ratio = W / H
    if fov_degrees_vertical:
        fov_v = fov_degrees_vertical
    elif fov_degrees_horizontal:
        fov_v, _ = fovv_and_fovh_degrees_given_either(None, fov_degrees_horizontal, aspect_ratio)
    else:
        fov_v = fov_v_from_camjson(camjson, aspect_ratio)

    # --- 5. Build camera-to-world and intrinsic matrices ---
    cam2world = np.float64(camjson['extrinsic_cam2world']).reshape(3, 4)
    cam2world = np.pad(cam2world, ((0, 1), (0, 0)))
    cam2world[-1, -1] = 1.0

    cam2screen = build_intrinsicmatrix_camtoscreenpix_pinhole_camera(
        fov_vertical_degrees=fov_v,
        screen_width=W,
        screen_height=H
    )
    world2screen = np.matmul(cam2screen, np.linalg.pinv(cam2world))
    screen2world = np.linalg.pinv(world2screen)

    # --- 6. Back-project depth to 3D points ---
    wpoints, imcoords = depth_image_to_4dscreencolumnvectors(depth)  # (4, N), (2, N)

    # --- 7. Distance filtering ---
    if max_distance is not None and np.isfinite(max_distance):
        keep_mask = depth.flatten() < max_distance
        wpoints = wpoints[:, keep_mask]
        imcoords = imcoords[:, keep_mask]

    # Transform to world space
    wpoints_world = np.ascontiguousarray(
        np.matmul(screen2world, wpoints).T[:, :3]
    )  # (N, 3)

    # Colors
    colors = rgb_resized.reshape(-1, 3)
    if max_distance is not None and np.isfinite(max_distance):
        colors = colors[keep_mask]

    # --- 8. Subsample ---
    if subsample_amt > 1:
        N = len(wpoints_world)
        if N == 0:
            pass
        else:
            keep = slice(None, None, subsample_amt)
            wpoints_world = wpoints_world[keep]
            colors = colors[keep]
            imcoords = imcoords[:, keep]

    # --- 9. Return point cloud ---
    return {
        'worldpoints': wpoints_world,
        'colors': colors,
        'pixcoords': imcoords,
        'depth_image': depth,
        'rgb_image': rgb_resized,
        'world2screen': world2screen,
        'screen_width': W,
        'screen_height': H
    }


def merge_clouds_world_points(clouds):
    """合并多个点云字典"""
    if isinstance(clouds, dict):
        return clouds
    if len(clouds) == 0:
        return None

    mergeable = ['worldpoints']
    if all('colors' in cl for cl in clouds):
        mergeable.append('colors')

    merged = {key: [] for key in mergeable}
    for cl in clouds:
        for key in mergeable:
            merged[key].append(cl[key])
    return {key: np.concatenate(val, axis=0) for key, val in merged.items()}


def visualize_clouds(clouds):
    """使用 Open3D 可视化点云"""
    import open3d as o3d

    if isinstance(clouds, dict):
        clouds = [clouds]

    assert len(clouds) >= 1, "No clouds to visualize"
    assert all(isinstance(c, dict) for c in clouds), "Clouds must be dicts"

    colored = all('colors' in c for c in clouds)
    pcd = o3d.geometry.PointCloud()
    pcd.points = o3d.utility.Vector3dVector(np.concatenate([c['worldpoints'] for c in clouds]))

    if colored:
        color_list = []
        for c in clouds:
            if c['colors'].dtype == np.uint8:
                color_list.append(np.float32(c['colors']) / 255.0)
            elif c['colors'].dtype == np.float32:
                assert c['colors'].min() >= 0 and c['colors'].max() <= 1.0, "Color values out of range [0,1]"
                color_list.append(c['colors'])
            else:
                raise TypeError(f"Unsupported color dtype: {c['colors'].dtype}")
        pcd.colors = o3d.utility.Vector3dVector(np.concatenate(color_list))

    o3d.visualization.draw([pcd])


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description="Reconstruct point clouds from Cyberpunk 2077 capture data (.npy + mp4)")
    parser.add_argument("data_dir", help="Directory containing frame_*.npy, frame_*.json, and capture.mp4")
    parser.add_argument("--frames", type=str, default="all",
                        help="Frame range: '0:100', '100,101,200', or 'all'")
    parser.add_argument("-max", "--max_distance_clip_cloud", type=float, default=np.inf,
                        help="Max distance to clip depth (default: inf)")
    parser.add_argument("-ss", "--subsample_amt", type=int, default=1,
                        help="Subsample every N-th point")
    parser.add_argument("-fovv", "--fov_degrees_vertical", type=float, default=None,
                        help="Override vertical FoV (degrees)")
    parser.add_argument("-fovh", "--fov_degrees_horizontal", type=float, default=None,
                        help="Override horizontal FoV (degrees)")
    parser.add_argument("-o", "--save_to_file", type=str, default="", help="Save merged point cloud to file (.ply, .pcd)")

    args = parser.parse_args()

    data_dir = args.data_dir
    video_path = os.path.join(data_dir, "capture.mp4")
    if not os.path.isfile(video_path):
        raise FileNotFoundError(f"Video file not found: {video_path}")

    # --- 构建帧索引 ---
    print("📊 Scanning for .npy depth files...")
    frame_indices = build_npy_frame_list(data_dir)
    total_frames = len(frame_indices)
    print(f"🎬 Found {total_frames} depth frames.")

    # --- 解析要处理的帧 ---
    if args.frames == "all":
        selected_indices = frame_indices
    elif ':' in args.frames:
        start, end = map(int, args.frames.split(':'))
        selected_indices = [i for i in frame_indices if start <= i < end]
    else:
        wanted = set(int(x.strip()) for x in args.frames.split(',') if x.strip().isdigit())
        selected_indices = [i for i in frame_indices if i in wanted]

    print(f"🚀 Processing {len(selected_indices)} frames: {selected_indices[:10]}{'...' if len(selected_indices)>10 else ''}")

    # --- 并行加载点云 ---
    loader = partial(
        load_frame_pointcloud,
        data_dir=data_dir,
        video_path=video_path,
        max_distance=args.max_distance_clip_cloud,
        subsample_amt=args.subsample_amt,
        fov_degrees_vertical=args.fov_degrees_vertical,
        fov_degrees_horizontal=args.fov_degrees_horizontal
    )

    clouds = process_map(loader, selected_indices, max_workers=None)
    clouds = [c for c in clouds if c is not None]  # Remove failed loads

    if len(clouds) == 0:
        print("❌ No valid frames loaded. Exiting.")
        exit(1)

    # --- 合并点云 ---
    merged_cloud = merge_clouds_world_points(clouds)
    print(f"📦 Merged {len(clouds)} frames into a cloud with {merged_cloud['worldpoints'].shape[0]} points.")

    # --- 保存 ---
    if args.save_to_file and len(args.save_to_file.strip()) > 1:
        save_cloud_to_file(merged_cloud, args.save_to_file)
        print(f"💾 Point cloud saved to: {args.save_to_file}")

    # --- 可视化 ---
    print("🎨 Visualizing point cloud...")
    visualize_clouds(merged_cloud)
