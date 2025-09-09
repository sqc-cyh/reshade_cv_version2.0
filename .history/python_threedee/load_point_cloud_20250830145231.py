#!/usr/bin/env python3
# Copyright (C) 2022 Jason Bunk
import os
import argparse
import json
import numpy as np
from PIL import Image
from game_camera import (
    vertical_fov_from_horizontal_fov_degrees,
    fovv_and_fovh_degrees_given_either,
)
from save_point_cloud_to_file import save_cloud_to_file
from misc_utils import files_glob
from functools import partial
from tqdm.contrib.concurrent import process_map


def fov_v_from_camjson(camjson:dict, screen_aspect_ratio_w_over_h:float):
    assert isinstance(camjson,dict), str(type(camjson))
    assert 'fov_v_degrees' in camjson or 'fov_h_degrees' in camjson, \
        f"FoV missing in camera/meta json. Keys: {sorted(list(camjson.keys()))}"
    if 'fov_v_degrees' in camjson:
        return float(camjson['fov_v_degrees'])
    return vertical_fov_from_horizontal_fov_degrees(
        float(camjson['fov_h_degrees']), screen_aspect_ratio_w_over_h
    )


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


# ---------- GTA -> OpenCV 适配与投影 ----------

def make_K(W:int, H:int, fov_v_deg:float):
    """构建像素坐标内参矩阵（主点居中）"""
    fy = H / (2.0 * np.tan(np.deg2rad(fov_v_deg) * 0.5))
    fx = fy * (W / H)
    cx = (W - 1) * 0.5
    cy = (H - 1) * 0.5
    K = np.array([[fx, 0., cx],
                  [0., fy, cy],
                  [0., 0.,  1.]], dtype=np.float64)
    return K, np.linalg.inv(K)


def adapt_R_gta_to_opencv(R_gta:np.ndarray) -> np.ndarray:
    """
    GTA meta 中 R 的列语义近似为 [right, forward, up].
    目标 OpenCV 相机系采用 [x, y, z] = [right, -up, forward].
    因此右乘固定列替换矩阵 M，即相机轴重排取符号：
    [r, f, u] -> [r, -u, f].
    """
    M = np.array([[1, 0, 0],
                  [0, 0, 1],
                  [0,-1, 0]], dtype=np.float64)
    return R_gta @ M


def world2screen_P(K:np.ndarray, R_cw:np.ndarray, C:np.ndarray) -> np.ndarray:
    """生成 3×4 投影矩阵 P = K [R_wc | t_wc]. 仅用于可选的调试或导出。"""
    R_wc = R_cw.T
    t_wc = - R_cw.T @ C
    P = K @ np.hstack([R_wc, t_wc])
    return P


def load_cloud_via_depth_and_camjson(
        depthfile:str,
        colored:bool,
        max_distance:float=None,
        subsample_amt:int=0,
        fov_degrees_vertical:float=None,
        fov_degrees_horizontal:float=None,
    ):
    """
    假设 depth.npy 为**相机坐标 Z 深度（米）**.
    若你的深度是“射线长度”，把 Xc 的构造改为 d_norm * D 即可.
    """
    if not isinstance(max_distance,float):
        assert max_distance in (None,'np.inf','inf',), str(max_distance)

    # 读数据
    if colored:
        depth, camjson, rgb = load_depth_and_camjson(depthfile, True)
        rgb_image = np.copy(rgb)
    else:
        depth, camjson = load_depth_and_camjson(depthfile, False)
    H, W = depth.shape

    # FoV
    if fov_degrees_vertical or fov_degrees_horizontal:
        fov_v, _ = fovv_and_fovh_degrees_given_either(
            fov_degrees_vertical, fov_degrees_horizontal, W/H
        )
    else:
        fov_v = fov_v_from_camjson(camjson, W/H)

    # 内参
    K, Kinv = make_K(W, H, fov_v)

    # 外参（GTA cam->world），并做轴适配到 OpenCV 语义
    assert 'extrinsic_cam2world' in camjson, str(sorted(list(camjson.keys())))
    M = np.float64(camjson['extrinsic_cam2world']).reshape((3,4))
    R_gta = M[:,:3].copy()                  # 3×3
    C     = M[:, 3:4].copy()                # 3×1（相机中心）
    R_cw  = adapt_R_gta_to_opencv(R_gta)    # 3×3（cam->world, OpenCV 语义）

    # 诊断
    detR = np.linalg.det(R_cw)
    ortho_err = np.linalg.norm(R_cw @ R_cw.T - np.eye(3), 'fro')
    print(f"[R_cw] det={detR:.6f}  ortho_err={ortho_err:.3e}")

    # 像素 -> 归一化射线
    uu, vv = np.meshgrid(np.arange(W), np.arange(H))
    uv1 = np.stack([uu.ravel(), vv.ravel(), np.ones_like(uu).ravel()], axis=0)  # 3×N
    d = Kinv @ uv1                                                                # 3×N

    # Z 深度恢复：Xc = d * (Z / d_z)
    Z = depth.ravel().astype(np.float64)
    if max_distance is not None and np.isfinite(max_distance):
        keep = np.isfinite(Z) & (Z < max_distance) & (Z > 0)
    else:
        keep = np.isfinite(Z) & (Z > 0)

    d  = d[:, keep]
    Z  = Z[keep]
    Xc = d * (Z / d[2,:])                                                         # 3×N

    # 相机系 -> 世界系
    Xw = (R_cw @ Xc) + C                                                          # 3×N
    wpoints = Xw.T.copy()                                                         # N×3

    # 颜色
    ret_ = {'worldpoints': wpoints,
            'screen_width': W, 'screen_height': H,
            'depth_image': depth}

    if colored:
        rgb = rgb.reshape(-1,3)[keep]
        ret_['colors'] = rgb
        ret_['rgb_image'] = rgb_image

    # 可选：投影矩阵（若你后续想保存/调试）
    ret_['world2screen'] = world2screen_P(K, R_cw, C)

    # 随机下采样（可选）
    if subsample_amt > 0:
        idx = np.random.permutation(wpoints.shape[0])[::subsample_amt]
        ret_['worldpoints'] = ret_['worldpoints'][idx]
        if 'colors' in ret_:
            ret_['colors'] = ret_['colors'][idx]

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
                assert cc['colors'].min() > -1e-6 and cc['colors'].max() < 1.000001, \
                    f"{cc['colors'].min()}, {cc['colors'].max()}"
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

    clouds = merge_clouds_world_points(process_map(
        partial(load_cloud_via_depth_and_camjson,
                colored=args.color_avail,
                max_distance=args.max_distance_clip_cloud,
                subsample_amt=args.subsample_amt,
                fov_degrees_vertical=args.fov_degrees_vertical,
                fov_degrees_horizontal=args.fov_degrees_horizontal),
        args.depth_files))

    if args.save_to_file and len(args.save_to_file) > 1:
        save_cloud_to_file(clouds, args.save_to_file)

    visualize_clouds(clouds)
