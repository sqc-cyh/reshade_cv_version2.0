#!/usr/bin/env python3
# Copyright (C) 2022 Jason Bunk
import os
import argparse
import json
import numpy as np
from PIL import Image
from game_camera import (
    vertical_fov_from_horizontal_fov_degrees,
    build_intrinsicmatrix_camtoscreenpix_pinhole_camera,
    # depth_image_to_4dscreencolumnvectors,  # 不再使用
    fovv_and_fovh_degrees_given_either,
)
from save_point_cloud_to_file import save_cloud_to_file
from misc_utils import files_glob
from functools import partial
from tqdm.contrib.concurrent import process_map


def fov_v_from_camjson(camjson: dict, screen_aspect_ratio_w_over_h: float):
    assert isinstance(camjson, dict), str(type(camjson))
    assert 'fov_v_degrees' in camjson or 'fov_h_degrees' in camjson, (
        "FoV 未提供：请在命令行或 camera/meta.json 中提供 fov_v_degrees 或 fov_h_degrees"
    )
    if 'fov_v_degrees' in camjson:
        return float(camjson['fov_v_degrees'])
    return vertical_fov_from_horizontal_fov_degrees(
        float(camjson['fov_h_degrees']), screen_aspect_ratio_w_over_h
    )


def load_depth_and_camjson(depthfile: str, and_rgb: bool):
    assert os.path.isfile(depthfile), depthfile
    if depthfile.endswith('.npy'):
        assert depthfile.endswith('_depth.npy'), depthfile
        depthbnam = depthfile[:-len('_depth.npy')]
        depth = np.load(depthfile, allow_pickle=False)
    else:
        assert depthfile.endswith('_depth.fpzip'), depthfile
        depthbnam = depthfile[:-len('_depth.fpzip')]
        import fpzip
        with open(depthfile, 'rb') as infile:
            depth = fpzip.decompress(infile.read())
        assert len(depth.shape) in (2, 4), str(depth.shape)
        if len(depth.shape) == 4:
            assert int(depth.shape[0]) == 1 and int(depth.shape[1]) == 1, str(depth.shape)
            depth = depth[0, 0, :, :]
    assert depth.dtype in (np.float32, np.float64), str(depth.dtype)
    assert len(depth.shape) == 2 and min(depth.shape) > 9, str(depth.shape)

    cmjfile = depthbnam + '_camera.json'
    if not os.path.isfile(cmjfile):
        cmjfile = depthbnam + '_meta.json'
    assert os.path.isfile(cmjfile), cmjfile
    with open(cmjfile, 'r') as infile:
        camjson = json.load(infile)
    assert isinstance(camjson, dict), str(type(camjson))

    if and_rgb:
        colorfile = depthbnam + '_RGB.png'
        assert os.path.isfile(colorfile), colorfile
        rgb = np.asarray(Image.open(colorfile).convert('RGB'))
        assert len(rgb.shape) == 3 and int(rgb.shape[2]) == 3, str(rgb.shape)
        assert rgb.shape[:2] == depth.shape[:2], f"{rgb.shape} vs {depth.shape}"
        return depth, camjson, rgb
    return depth, camjson


def random_subsample(every_nth, *arrays):
    n = len(arrays[0])
    if every_nth <= 1 or n == 0:
        return arrays if len(arrays) > 1 else arrays[0]
    perm = np.random.permutation(n)[::every_nth]
    if len(arrays) == 1:
        return arrays[0][perm]
    return tuple(arr[perm] for arr in arrays)


def backproject_depth_to_world(
    depth_image: np.ndarray,
    cam2world_4x4: np.ndarray,
    K: np.ndarray,
    depth_type: str = "range",  # "range"（射线距离）或 "z"（透视深度）
):
    """将深度图反投影为世界坐标点云。"""
    H, W = depth_image.shape
    fx, fy = K[0, 0], K[1, 1]
    cx, cy = K[0, 2], K[1, 2]

    # 像素网格（与 numpy / image 坐标一致：u=x(列), v=y(行)）
    uu, vv = np.meshgrid(np.arange(W, dtype=np.float64),
                         np.arange(H, dtype=np.float64),
                         indexing='xy')

    D = depth_image.astype(np.float64)

    # 有效深度掩码
    valid = np.isfinite(D) & (D > 0.0)
    if not np.any(valid):
        return np.empty((0, 3), dtype=np.float64), np.empty((0, 2), dtype=np.float64)

    # 归一化坐标
    xn = (uu - cx) / fx
    yn = (vv - cy) / fy

    if depth_type == "range":
        # 射线距离 -> 透视深度 Z
        Z = D / np.sqrt(xn * xn + yn * yn + 1.0)
    else:
        # 已经是相机系 Z
        Z = D

    # 相机系坐标
    X = xn * Z
    Y = yn * Z

    # 仅保留有效点
    X = X[valid]
    Y = Y[valid]
    Z = Z[valid]
    pixcoords = np.stack([uu[valid], vv[valid]], axis=1)

    # 组装齐次并变换到世界系
    cam_pts = np.stack([X, Y, Z, np.ones_like(Z)], axis=0)  # (4, N)
    world_pts = (cam2world_4x4 @ cam_pts).T[:, :3]          # (N, 3)

    return world_pts.astype(np.float64), pixcoords.astype(np.float64)


def load_cloud_via_depth_and_camjson(
    depthfile: str,
    colored: bool,
    max_distance: float = None,
    subsample_amt: int = 0,
    fov_degrees_vertical: float = None,
    fov_degrees_horizontal: float = None,
    depth_type: str = "range",
):
    if not isinstance(max_distance, float):
        assert max_distance in (None, 'np.inf', 'inf',), str(max_distance)

    if colored:
        depth, camjson, rgb = load_depth_and_camjson(depthfile, True)
        rgb_image = np.copy(rgb)
    else:
        depth, camjson = load_depth_and_camjson(depthfile, False)

    depth_image = np.copy(depth)
    screen_width = int(depth.shape[1])
    screen_height = int(depth.shape[0])

    if fov_degrees_vertical or fov_degrees_horizontal:
        fov_v, _ = fovv_and_fovh_degrees_given_either(
            fov_degrees_vertical, fov_degrees_horizontal, screen_width / screen_height
        )
    else:
        fov_v = fov_v_from_camjson(camjson, screen_width / screen_height)

    # 外参 cam->world
    assert 'extrinsic_cam2world' in camjson, str(sorted(list(camjson.keys())))
    cam2world = np.float64(camjson['extrinsic_cam2world']).reshape((3, 4))
    cam2world = np.pad(cam2world, ((0, 1), (0, 0)))
    cam2world[-1, -1] = 1.0

    # 内参
    K = build_intrinsicmatrix_camtoscreenpix_pinhole_camera(
        fov_vertical_degrees=fov_v, screen_width=screen_width, screen_height=screen_height
    )

    # 距离裁剪（基于原始距离度量）
    if (max_distance is not None) and np.isfinite(max_distance):
        depth_image = np.where(depth_image < max_distance, depth_image, np.nan)

    # 反投影到世界坐标
    wpoints, imcoords = backproject_depth_to_world(
        depth_image, cam2world, K, depth_type=depth_type
    )

    # 颜色对齐
    if colored:
        rgb = rgb.reshape((-1, 3))
        # 根据 imcoords 重新索引颜色
        # 注意 imcoords 是浮点像素坐标，来源于有效 mask，故四舍五入到最近像素
        u = np.clip(np.rint(imcoords[:, 0]).astype(np.int64), 0, screen_width - 1)
        v = np.clip(np.rint(imcoords[:, 1]).astype(np.int64), 0, screen_height - 1)
        rgb = rgb.reshape((screen_height, screen_width, 3))[v, u]
        # subsample 时一并处理
    # 下采样（可选）
    if subsample_amt > 0:
        if colored:
            wpoints, imcoords, rgb = random_subsample(subsample_amt, wpoints, imcoords, rgb)
        else:
            wpoints, imcoords = random_subsample(subsample_amt, wpoints, imcoords)

    ret_ = {
        'worldpoints': np.ascontiguousarray(wpoints),
        'pixcoords': imcoords,
        'K': K,
        'cam2world': cam2world,
        'screen_width': screen_width,
        'screen_height': screen_height,
        'depth_image': depth_image,
    }
    if colored:
        ret_['colors'] = rgb
        ret_['rgb_image'] = rgb_image
    return ret_


def merge_clouds_world_points(clouds):
    if isinstance(clouds, dict):
        return clouds
    mergeable = ['worldpoints', ]
    if all(('colors' in cl) for cl in clouds):
        mergeable.append('colors')
    merged = {key: [] for key in mergeable}
    for cl in clouds:
        for key in mergeable:
            merged[key].append(cl[key])
    return {key: np.concatenate(val, axis=0) for key, val in merged.items()}


def visualize_clouds(clouds):
    import open3d
    if isinstance(clouds, dict):
        clouds = [clouds, ]
    else:
        assert len(clouds) >= 1, str(clouds)
    assert all(isinstance(cc, dict) for cc in clouds), "所有元素应为 dict"

    colored = all(('colors' in cc) for cc in clouds)

    pts = np.concatenate([cc['worldpoints'] for cc in clouds], axis=0)
    o3dcloud = open3d.geometry.PointCloud()
    o3dcloud.points = open3d.utility.Vector3dVector(pts)

    if colored:
        colors_list = []
        for cc in clouds:
            col = cc['colors']
            if col.dtype == np.uint8:
                col = (col.astype(np.float32) / 255.0)
            else:
                assert col.dtype in (np.float32, np.float64)
            colors_list.append(col.astype(np.float32))
        cols = np.concatenate(colors_list, axis=0)
        o3dcloud.colors = open3d.utility.Vector3dVector(cols)

    open3d.visualization.draw([o3dcloud])


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument("depth_files", nargs="+")
    parser.add_argument("-max", "--max_distance_clip_cloud", type=float, default=1e9)
    parser.add_argument("-ss", "--subsample_amt", type=int, default=0)
    parser.add_argument("-nc", "--no_color_avail", action="store_false", dest="color_avail")
    parser.add_argument("-fovv", "--fov_degrees_vertical", type=float,
                        help="可选；若 camera/meta.json 已给出则不需要")
    parser.add_argument("-fovh", "--fov_degrees_horizontal", type=float)
    parser.add_argument("--depth_type", choices=["range", "z"], default="range",
                        help="深度类型：range=射线距离（真实距离），z=相机系透视深度")
    parser.add_argument("-o", "--save_to_file", type=str, default="")
    args = parser.parse_args()

    args.depth_files = files_glob(args.depth_files)

    worker = partial(
        load_cloud_via_depth_and_camjson,
        colored=args.color_avail,
        max_distance=args.max_distance_clip_cloud,
        subsample_amt=args.subsample_amt,
        fov_degrees_vertical=args.fov_degrees_vertical,
        fov_degrees_horizontal=args.fov_degrees_horizontal,
        depth_type=args.depth_type,
    )
    clouds = merge_clouds_world_points(process_map(worker, args.depth_files))

    if args.save_to_file and len(args.save_to_file) > 1:
        save_cloud_to_file(clouds, args.save_to_file)

    visualize_clouds(clouds)
