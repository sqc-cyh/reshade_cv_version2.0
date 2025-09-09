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
    assert 'fov_v_degrees' in camjson or 'fov_h_degrees' in camjson, f"Did you forget to provide the FoV on the command line? The camera/meta json doesn't seem to provide FoV; here are the keys in the json: {sorted(list(camjson.keys()))}"
    if 'fov_v_degrees' in camjson:
        return float(camjson['fov_v_degrees'])
    return vertical_fov_from_horizontal_fov_degrees(float(camjson['fov_h_degrees']), screen_aspect_ratio_w_over_h)


def load_depth_and_camjson(depthfile:str, and_rgb:bool):
    assert os.path.isfile(depthfile), depthfile
    if depthfile.endswith('.npy'):
        assert depthfile.endswith('_depth.npy'), depthfile
        depthbnam = depthfile[:-len('_depth.npy')]
        depth = np.load(depthfile, allow_pickle=False)
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


def load_cloud_via_depth_and_camjson(
        depthfile:str, colored:bool,
        max_distance:float=None, subsample_amt:int=0,
        fov_degrees_vertical:float=None, fov_degrees_horizontal:float=None):

    if not isinstance(max_distance,float):
        assert max_distance in (None,'np.inf','inf',), str(max_distance)

    if colored:
        depth, camjson, rgb = load_depth_and_camjson(depthfile, True)
        rgb_image = np.copy(rgb)
    else:
        depth, camjson = load_depth_and_camjson(depthfile, False)

    depth_image = np.asarray(depth, dtype=np.float64)       # 已为“米”的相机轴向 Z
    H, W = depth_image.shape

    # FoV
    if fov_degrees_vertical or fov_degrees_horizontal:
        fov_v, _ = fovv_and_fovh_degrees_given_either(
            fov_degrees_vertical, fov_degrees_horizontal, W/H)
    else:
        fov_v = fov_v_from_camjson(camjson, W/H)

    # 外参 cam->world（4x4）
    assert 'extrinsic_cam2world' in camjson, str(sorted(list(camjson.keys())))
    Tcw = np.float64(camjson['extrinsic_cam2world']).reshape((3,4))
    Tcw = np.pad(Tcw, ((0,1),(0,0))); Tcw[-1,-1] = 1.0

    # 内参（由垂直 FoV 构造）
    fy = H / (2.0 * np.tan(np.deg2rad(fov_v) / 2.0))
    fx = fy * (W / H)
    cx, cy = (W - 1) / 2.0, (H - 1) / 2.0

    # 反投影：像素→相机坐标
    u = np.arange(W); v = np.arange(H)
    uu, vv = np.meshgrid(u, v)

    Z = depth_image
    mask = np.isfinite(Z) & (Z > 1e-4)
    if max_distance is not None and np.isfinite(max_distance):
        mask &= (Z < max_distance)

    uu = uu[mask].astype(np.float64)
    vv = vv[mask].astype(np.float64)
    Z  = Z[mask].astype(np.float64)

    X = (uu - cx) / fx * Z
    Y = (vv - cy) / fy * Z

    Pc = np.stack([X, Y, Z, np.ones_like(Z)], axis=0)   # 4 x N

    # 相机→世界：一次左乘 Tcw（避免任何伪逆）
    Pw = (Tcw @ Pc).T[:, :3]                             # N x 3
    imcoords = np.stack([uu, vv], axis=1)                # N x 2

    if colored:
        rgb = rgb.reshape(H, W, 3)[mask]
    # 随机下采样（可选）
    if subsample_amt > 0:
        idx = np.random.permutation(Pw.shape[0])[::subsample_amt]
        Pw = Pw[idx]; imcoords = imcoords[idx]
        if colored:
            rgb = rgb[idx]

    ret_ = {
        'worldpoints': np.ascontiguousarray(Pw),
        'pixcoords':   imcoords,
        'screen_width':  W, 'screen_height': H,
        'depth_image': depth_image
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
    assert all([isinstance(cc,dict) for cc in clouds]), str(type(clouds))+'\n'+', '.join([str(type(cc)) for cc in clouds])
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
                assert cc['colors'].min() > -1e-6 and cc['colors'].max() < 1.000001, str(cc['colors'].min())+', '+str(cc['colors'].max())
                colors.append(cc)
        o3dcloud.colors = open3d.utility.Vector3dVector(np.concatenate(colors))
    open3d.visualization.draw([o3dcloud])


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument("depth_files", nargs="+")
    parser.add_argument("-max", "--max_distance_clip_cloud", type=float, default=1e9)
    parser.add_argument("-ss", "--subsample_amt", type=int, default=0)
    parser.add_argument("-nc", "--no_color_avail", action="store_false", dest="color_avail")
    parser.add_argument("-fovv", "--fov_degrees_vertical", type=float, help="optional if already in camera meta json")
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