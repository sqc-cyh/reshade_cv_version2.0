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
    depth_image_to_4dscreencolumnvectors,
    fovv_and_fovh_degrees_given_either,
)
from save_point_cloud_to_file import save_cloud_to_file
from misc_utils import files_glob
from functools import partial
from tqdm.contrib.concurrent import process_map


def fov_v_from_camjson(camjson:dict, screen_aspect_ratio_w_over_h:float):
    assert isinstance(camjson,dict), str(type(camjson))
    assert 'fov_v_degrees' in camjson or 'fov_h_degrees' in camjson, \
        f"FoV missing in json; keys: {sorted(list(camjson.keys()))}"
    if 'fov_v_degrees' in camjson:
        return float(camjson['fov_v_degrees'])
    return vertical_fov_from_horizontal_fov_degrees(float(camjson['fov_h_degrees']),
                                                    screen_aspect_ratio_w_over_h)


def load_depth_and_camjson(depthfile:str, and_rgb:bool):
    assert os.path.isfile(depthfile), depthfile
    if depthfile.endswith('.npy'):
        assert depthfile.endswith('_depth.npy'), depthfile
        depthbnam = depthfile[:-len('_depth.npy')]
        depth = np.load(depthfile, allow_pickle=False)  # HxW, float32/64
    else:
        assert depthfile.endswith('_depth.fpzip'), depthfile
        depthbnam = depthfile[:-len('_depth.fpzip')]
        import fpzip
        with open(depthfile,'rb') as infile:
            depth = fpzip.decompress(infile.read())
        assert len(depth.shape) in (2,4), str(depth.shape)
        if len(depth.shape) == 4:
            assert int(depth.shape[0]) == 1 and int(depth.shape[1]) == 1, str(depth.shape)
            depth = depth[0,0,:,:]
    assert depth.dtype in (np.float32, np.float64), str(depth.dtype)
    assert len(depth.shape) == 2 and min(depth.shape) > 9, str(depth.shape)

    cmjfile = depthbnam+'_camera.json'
    if not os.path.isfile(cmjfile):
        cmjfile = depthbnam+'_meta.json'
    assert os.path.isfile(cmjfile), cmjfile
    with open(cmjfile,'r') as infile:
        camjson = json.load(infile)
    assert isinstance(camjson,dict), str(type(camjson))

    if and_rgb:
        colorfile = depthbnam+'_RGB.png'
        assert os.path.isfile(colorfile), colorfile
        rgb = np.asarray(Image.open(colorfile).convert('RGB'))
        assert len(rgb.shape) == 3 and int(rgb.shape[2]) == 3, str(rgb.shape)
        assert rgb.shape[:2] == depth.shape[:2], f"{rgb.shape} vs {depth.shape}"
        return depth, camjson, rgb
    return depth, camjson


def random_subsample(every_nth, *arrays):
    perm = np.random.permutation(len(arrays[0]))[::every_nth]
    if len(arrays) == 1:
        return arrays[0][perm]
    return tuple((arr[perm] for arr in arrays))


def load_cloud_via_depth_and_camjson(depthfile:str,
            colored:bool,
            max_distance:float=None,
            subsample_amt:int=0,
            fov_degrees_vertical:float=None,
            fov_degrees_horizontal:float=None,
            ):
    """
    假设 depth(.npy/.fpzip) 存的是射线距离 r(u,v)（单位米）。
    本函数先将 r → z，再按针孔模型反投影。
    """
    if not isinstance(max_distance,float):
        assert max_distance in (None,'np.inf','inf',), str(max_distance)

    # --- 读取 r 与相机参数 ---
    if colored:
        ray, camjson, rgb = load_depth_and_camjson(depthfile, True)
        rgb_image = np.copy(rgb)
    else:
        ray, camjson = load_depth_and_camjson(depthfile, False)

    ray_image = np.copy(ray).astype(np.float64)  # HxW, 射线距离 r
    screen_height, screen_width = int(ray_image.shape[0]), int(ray_image.shape[1])

    # FoV
    if fov_degrees_vertical or fov_degrees_horizontal:
        fov_v, _ = fovv_and_fovh_degrees_given_either(
            fov_degrees_vertical, fov_degrees_horizontal, screen_width/screen_height)
    else:
        fov_v = fov_v_from_camjson(camjson, screen_width / screen_height)

    # --- 内参（从 fov_v 推导 fy，再由宽高比推 fx；主点取图像中心） ---
    fy = (screen_height / 2.0) / np.tan(np.deg2rad(fov_v) / 2.0)
    fx = fy * (screen_width / screen_height)
    cx = (screen_width - 1) / 2.0
    cy = (screen_height - 1) / 2.0

    # --- 外参 ---
    assert 'extrinsic_cam2world' in camjson, str(sorted(list(camjson.keys())))
    cam2world = np.float64(camjson['extrinsic_cam2world']).reshape((3,4))
    cam2world = np.pad(cam2world, ((0,1),(0,0)))  # 4x4
    cam2world[-1,-1] = 1.

    # 如需做坐标系手性变换，可在此处插入 conversion_matrix，再右乘/左乘
    # conversion_matrix = np.array([
    #     [1,  0,  0, 0],
    #     [0,  0,  1, 0],
    #     [0, -1,  0, 0],
    #     [0,  0,  0, 1]
    # ])
    # cam2world = cam2world @ conversion_matrix
    # cam2world = np.linalg.inv(cam2world)

    # --- 相机内参矩阵（像素坐标到屏幕） ---
    cam2screen = build_intrinsicmatrix_camtoscreenpix_pinhole_camera(
        fov_vertical_degrees=fov_v, screen_width=screen_width, screen_height=screen_height)
    world2screen = np.matmul(cam2screen, np.linalg.pinv(cam2world))
    screen2world = np.linalg.pinv(world2screen)

    # --- r → z（逐像素角度校正） ---
    u = np.arange(screen_width, dtype=np.float64)
    v = np.arange(screen_height, dtype=np.float64)
    uu, vv = np.meshgrid(u, v)  # HxW
    x = (uu - cx) / fx
    y = (vv - cy) / fy
    dz = 1.0 / np.sqrt(1.0 + x * x + y * y)   # = d_z(u,v)
    z_image = ray_image * dz                  # r -> z

    # --- （可选）按射线距离裁剪 ---
    if max_distance is not None and np.isfinite(max_distance):
        depth_mask_keep = np.less(ray_image, float(max_distance)).flatten()
    else:
        depth_mask_keep = None

    # --- 生成屏幕齐次列向量并反投影到世界坐标 ---
    wpoints, imcoords = depth_image_to_4dscreencolumnvectors(z_image)  # 期望输入 z
    if depth_mask_keep is not None:
        wpoints = np.stack([wpoints[ii,:][depth_mask_keep] for ii in range(wpoints.shape[0])], axis=0)
        imcoords = np.stack([imcoords[:,ii][depth_mask_keep] for ii in range(imcoords.shape[1])], axis=1)

    if colored:
        rgb = rgb.reshape((-1,3))
        if depth_mask_keep is not None:
            rgb = np.stack([rgb[:,ii][depth_mask_keep] for ii in range(rgb.shape[1])], axis=1)

    world_points = np.ascontiguousarray(np.matmul(screen2world, wpoints).transpose()[:,:3])

    # --- 下采样（可选） ---
    if subsample_amt > 0:
        if colored:
            world_points, imcoords, rgb = random_subsample(subsample_amt, world_points, imcoords, rgb)
        else:
            world_points, imcoords = random_subsample(subsample_amt, world_points, imcoords)

    ret_ = {
        'worldpoints': world_points,
        'pixcoords': imcoords,
        'world2screen': world2screen,
        'screen_width': screen_width,
        'screen_height': screen_height,
        # 返回用于调试：z 与 r
        'depth_image': z_image.astype(np.float32),   # 现在的“深度图”= z
        'ray_image': ray_image.astype(np.float32),   # 原始射线距离 r
        'fx': fx, 'fy': fy, 'cx': cx, 'cy': cy, 'fov_v_degrees': float(fov_v),
    }
    if colored:
        ret_['colors'] = rgb
        ret_['rgb_image'] = rgb_image
    return ret_


def merge_clouds_world_points(clouds):
    if isinstance(clouds,dict):
        return clouds
    mergeable = ['worldpoints',]
    if all(['colors' in cl for cl in clouds]):
        mergeable.append('colors')
    merged = {key:[] for key in mergeable}
    for cl in clouds:
        for key in mergeable:
            merged[key].append(cl[key])
    return {key:np.concatenate(val,axis=0) for key,val in merged.items()}


def visualize_clouds(clouds):
    import open3d
    if isinstance(clouds,dict):
        clouds = [clouds,]
    else:
        assert len(clouds) >= 1, str(clouds)
    assert all([isinstance(cc,dict) for cc in clouds]), \
        str(type(clouds))+'\n'+', '.join([str(type(cc)) for cc in clouds])
    colored = all(['colors' in cc for cc in clouds])
    o3dcloud = open3d.geometry.PointCloud()
    o3dcloud.points = open3d.utility.Vector3dVector(np.concatenate([cc['worldpoints'] for cc in clouds]))
    if colored:
        colors = []
        for cc in clouds:
            if cc['colors'].dtype == np.uint8:
                colors.append(np.float32(cc['colors'])/255.)
            else:
                assert cc['colors'].dtype == np.float32, str(cc['colors'].dtype)
                assert cc['colors'].min() > -1e-6 and cc['colors'].max() < 1.000001, \
                    str(cc['colors'].min())+', '+str(cc['colors'].max())
                colors.append(cc)
        o3dcloud.colors = open3d.utility.Vector3dVector(np.concatenate(colors))
    open3d.visualization.draw([o3dcloud])


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument("depth_files", nargs="+")
    parser.add_argument("-max", "--max_distance_clip_cloud", type=float, default=1e9,
                        help="clip by ray-length r (meters)")
    parser.add_argument("-ss", "--subsample_amt", type=int, default=0)
    parser.add_argument("-nc", "--no_color_avail", action="store_false", dest="color_avail")
    parser.add_argument("-fovv", "--fov_degrees_vertical", type=float,
                        help="optional if already in camera meta json")
    parser.add_argument("-fovh", "--fov_degrees_horizontal", type=float)
    parser.add_argument("-o", "--save_to_file", type=str, default="")
    args = parser.parse_args()

    args.depth_files = files_glob(args.depth_files)

    clouds = merge_clouds_world_points(process_map(partial(load_cloud_via_depth_and_camjson,
        colored=args.color_avail, max_distance=args.max_distance_clip_cloud,
        subsample_amt=args.subsample_amt,
        fov_degrees_vertical=args.fov_degrees_vertical,
        fov_degrees_horizontal=args.fov_degrees_horizontal), args.depth_files))

    if args.save_to_file and len(args.save_to_file) > 1:
        save_cloud_to_file(clouds, args.save_to_file)

    visualize_clouds(clouds)
