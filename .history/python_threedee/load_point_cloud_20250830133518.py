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


def fov_v_from_camjson(camjson: dict, screen_aspect_ratio_w_over_h: float):
    assert isinstance(camjson, dict), str(type(camjson))
    assert 'fov_v_degrees' in camjson or 'fov_h_degrees' in camjson, (
        "Did you forget to provide the FoV on the command line? "
        f"The camera/meta json doesn't seem to provide FoV; keys: {sorted(list(camjson.keys()))}"
    )
    if 'fov_v_degrees' in camjson:
        return float(camjson['fov_v_degrees'])
    return vertical_fov_from_horizontal_fov_degrees(float(camjson['fov_h_degrees']), screen_aspect_ratio_w_over_h)


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
    perm = np.random.permutation(len(arrays[0]))[::every_nth]
    if len(arrays) == 1:
        return arrays[0][perm]
    return tuple((arr[perm] for arr in arrays))


# ---------- New: helpers for Euclidean-depth back-projection ----------
def _construct_K_from_fov(screen_width: int, screen_height: int, fov_v_deg: float,
                          fx_scale: float = 1.0, fy_scale: float = 1.0,
                          principal_center_halfpx: bool = True):
    f = 0.5 * screen_height / np.tan(0.5 * np.deg2rad(fov_v_deg))
    fx = f * fx_scale
    fy = f * fy_scale
    if principal_center_halfpx:
        cx = (screen_width - 1) * 0.5
        cy = (screen_height - 1) * 0.5
    else:
        cx = screen_width * 0.5
        cy = screen_height * 0.5
    K = np.array([[fx, 0., cx],
                  [0., fy, cy],
                  [0., 0., 1.]], dtype=np.float64)
    return K, fx, fy, cx, cy


def _backproject_euclidean(depth_image: np.ndarray,
                           camjson: dict,
                           fov_v: float,
                           fx_scale: float = 1.0, fy_scale: float = 1.0,
                           principal_center_halfpx: bool = True,
                           flip_y_sign: bool = True,
                           depth_scale: float = 1.0, depth_bias: float = 0.0,
                           max_distance: float = None,
                           rgb: np.ndarray = None):
    H, W = depth_image.shape[:2]
    K, fx, fy, cx, cy = _construct_K_from_fov(W, H, fov_v, fx_scale, fy_scale, principal_center_halfpx)

    u, v = np.meshgrid(np.arange(W), np.arange(H))
    if principal_center_halfpx:
        u = u + 0.5
        v = v + 0.5

    x = (u - cx) / fx
    y = (v - cy) / fy
    if flip_y_sign:
        y = -y

    ray = np.stack([x, y, np.ones_like(x)], axis=-1)
    ray = ray / np.linalg.norm(ray, axis=-1, keepdims=True)

    t = depth_scale * depth_image + depth_bias
    if max_distance is not None and np.isfinite(max_distance):
        mask = (t < max_distance) & np.isfinite(t) & (t > 0)
    else:
        mask = np.isfinite(t) & (t > 0)

    pts_cam = (t[..., None] * ray)[mask]
    pts_cam_h = np.concatenate([pts_cam, np.ones((pts_cam.shape[0], 1), dtype=pts_cam.dtype)], axis=1)

    C = np.float64(camjson['extrinsic_cam2world']).reshape((3, 4))
    cam2world = np.vstack([C, [0, 0, 0, 1]])
    pts_world = (cam2world @ pts_cam_h.T).T[:, :3]

    imcoords = np.stack([u[mask], v[mask]], axis=1)

    colors = None
    if rgb is not None:
        # rgb shape: H x W x 3
        flat_idx = (v.astype(np.int64) * W + u.astype(np.int64))[mask].astype(np.int64)
        colors = rgb.reshape(H * W, 3)[flat_idx]

    return pts_world, imcoords, colors, K
# ---------------------------------------------------------------------


def load_cloud_via_depth_and_camjson(depthfile: str,
                                     colored: bool,
                                     max_distance: float = None,
                                     subsample_amt: int = 0,
                                     fov_degrees_vertical: float = None,
                                     fov_degrees_horizontal: float = None,
                                     # New switches / knobs:
                                     depth_is_euclidean: bool = False,
                                     fx_scale: float = 1.0, fy_scale: float = 1.0,
                                     principal_center_halfpx: bool = True,
                                     flip_y_sign: bool = True,
                                     depth_scale: float = 1.0,
                                     depth_bias: float = 0.0):
    if not isinstance(max_distance, float):
        assert max_distance in (None, 'np.inf', 'inf',), str(max_distance)
    if colored:
        depth, camjson, rgb = load_depth_and_camjson(depthfile, True)
        rgb_image = np.copy(rgb)
    else:
        depth, camjson = load_depth_and_camjson(depthfile, False)
        rgb = None
        rgb_image = None

    depth_image = np.copy(depth)
    screen_width = int(depth.shape[1])
    screen_height = int(depth.shape[0])

    if fov_degrees_vertical or fov_degrees_horizontal:
        fov_v, _ = fovv_and_fovh_degrees_given_either(
            fov_degrees_vertical, fov_degrees_horizontal, screen_width / screen_height)
    else:
        fov_v = fov_v_from_camjson(camjson, screen_width / screen_height)

    assert 'extrinsic_cam2world' in camjson, str(sorted(list(camjson.keys())))

    # --- New branch: Euclidean (ray-distance) back-projection ---
    if depth_is_euclidean:
        wpoints, imcoords, colors, _K = _backproject_euclidean(
            depth_image, camjson, fov_v,
            fx_scale=fx_scale, fy_scale=fy_scale,
            principal_center_halfpx=principal_center_halfpx,
            flip_y_sign=flip_y_sign,
            depth_scale=depth_scale, depth_bias=depth_bias,
            max_distance=max_distance,
            rgb=rgb if colored else None
        )

        if subsample_amt > 0:
            if colored and colors is not None:
                wpoints, imcoords, colors = random_subsample(subsample_amt, wpoints, imcoords, colors)
            else:
                wpoints, imcoords = random_subsample(subsample_amt, wpoints, imcoords)

        ret_ = {
            'worldpoints': np.ascontiguousarray(wpoints),
            'pixcoords': imcoords,
            'screen_width': screen_width,
            'screen_height': screen_height,
            'depth_image': depth_image
        }
        if colored and colors is not None:
            ret_['colors'] = colors
            ret_['rgb_image'] = rgb_image
        return ret_

    # --- Original z_cam-style inverse-projection branch ---
    cam2world = np.float64(camjson['extrinsic_cam2world']).reshape((3, 4))
    cam2world = np.pad(cam2world, ((0, 1), (0, 0)))
    cam2world[-1, -1] = 1.

    cam2screen = build_intrinsicmatrix_camtoscreenpix_pinhole_camera(
        fov_vertical_degrees=fov_v,
        screen_width=screen_width,
        screen_height=screen_height
    )
    world2screen = np.matmul(cam2screen, np.linalg.pinv(cam2world))
    screen2world = np.linalg.pinv(world2screen)

    wpoints, imcoords = depth_image_to_4dscreencolumnvectors(depth)
    if colored:
        rgb = rgb.reshape((-1, 3))

    if max_distance is not None and np.isfinite(max_distance):
        depth_mask_keep = np.less(depth, max_distance).flatten()
        wpoints = np.stack([wpoints[ii, :][depth_mask_keep] for ii in range(wpoints.shape[0])], axis=0)
        imcoords = np.stack([imcoords[:, ii][depth_mask_keep] for ii in range(imcoords.shape[1])], axis=1)
        if colored:
            rgb = np.stack([rgb[:, ii][depth_mask_keep] for ii in range(rgb.shape[1])], axis=1)

    wpoints = np.ascontiguousarray(np.matmul(screen2world, wpoints).transpose()[:, :3])

    if subsample_amt > 0:  # quick and dirty random subsampling... voxel subsampling is nicer
        if colored:
            wpoints, imcoords, rgb = random_subsample(subsample_amt, wpoints, imcoords, rgb)
        else:
            wpoints, imcoords = random_subsample(subsample_amt, wpoints, imcoords)

    ret_ = {'worldpoints': wpoints, 'pixcoords': imcoords, 'world2screen': world2screen,
            'screen_width': screen_width, 'screen_height': screen_height, 'depth_image': depth_image}
    if colored:
        ret_['colors'] = rgb
        ret_['rgb_image'] = rgb_image
    return ret_


def merge_clouds_world_points(clouds):
    if isinstance(clouds, dict):
        return clouds
    mergeable = ['worldpoints', ]
    if all(['colors' in cl for cl in clouds]):
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
    assert all([isinstance(cc, dict) for cc in clouds]), str(type(clouds)) + '\n' + ', '.join([str(type(cc)) for cc in clouds])
    colored = all(['colors' in cc for cc in clouds])
    o3dcloud = open3d.geometry.PointCloud()
    o3dcloud.points = open3d.utility.Vector3dVector(np.concatenate([cc['worldpoints'] for cc in clouds]))
    if colored:
        colors = []
        for cc in clouds:
            if cc['colors'].dtype == np.uint8:
                colors.append(np.float32(cc['colors']) / 255.)
            else:
                assert cc['colors'].dtype == np.float32 or cc['colors'].dtype == np.float64, str(cc['colors'].dtype)
                assert cc['colors'].min() > -1e-6 and cc['colors'].max() < 1.000001, f"{cc['colors'].min()}, {cc['colors'].max()}"
                colors.append(cc['colors'])
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

    # New: Euclidean-depth back-projection + calibration knobs
    parser.add_argument("--depth_is_euclidean", action="store_true",
                        help="If set, depth.npy stores Euclidean ray distance (meters), not z_cam.")
    parser.add_argument("--fx_scale", type=float, default=1.0,
                        help="Scale factor applied to fx derived from vertical FoV.")
    parser.add_argument("--fy_scale", type=float, default=1.0,
                        help="Scale factor applied to fy derived from vertical FoV.")
    parser.add_argument("--no_halfpx", action="store_true",
                        help="Disable half-pixel principal center convention.")
    parser.add_argument("--flip_y_sign", action="store_true",
                        help="Flip the sign of y when building camera rays (DX vs CV conventions).")
    parser.add_argument("--depth_scale", type=float, default=1.0,
                        help="Global scale alpha to apply to metric depth before back-projection.")
    parser.add_argument("--depth_bias", type=float, default=0.0,
                        help="Global bias beta (in meters) to apply to metric depth before back-projection.")

    args = parser.parse_args()
    args.depth_files = files_glob(args.depth_files)

    partial_loader = partial(
        load_cloud_via_depth_and_camjson,
        colored=args.color_avail,
        max_distance=args.max_distance_clip_cloud,
        subsample_amt=args.subsample_amt,
        fov_degrees_vertical=args.fov_degrees_vertical,
        fov_degrees_horizontal=args.fov_degrees_horizontal,
        depth_is_euclidean=args.depth_is_euclidean,
        fx_scale=args.fx_scale,
        fy_scale=args.fy_scale,
        principal_center_halfpx=(not args.no_halfpx),
        flip_y_sign=args.flip_y_sign,
        depth_scale=args.depth_scale,
        depth_bias=args.depth_bias,
    )

    clouds = merge_clouds_world_points(process_map(partial_loader, args.depth_files))

    if args.save_to_file and len(args.save_to_file) > 1:
        save_cloud_to_file(clouds, args.save_to_file)

    visualize_clouds(clouds)
