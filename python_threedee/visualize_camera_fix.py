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

def apply_transform_to_cam2world(cam2world: np.ndarray, S: np.ndarray):
    """将3D变换S应用到cam2world矩阵：新坐标系下，点P_new = S @ P_old"""
    assert cam2world.shape == (4, 4)
    assert S.shape == (3, 3)
    homog_S = np.eye(4, dtype=S.dtype)
    homog_S[:3, :3] = S
    return cam2world @ homog_S 

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
    # assert os.path.isfile(cmjfile), cmjfile
        if not os.path.isfile(cmjfile):
            print(f"[警告] 跳过文件 {depthfile}：未找到相机参数文件 ({cmjfile})")
            return None
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
            transform_S: np.ndarray = None,  # 新增参数：3x3 变换矩阵
            ):
    result = load_depth_and_camjson(depthfile, colored)
    if result is None:
        return None
    if not isinstance(max_distance,float):
        assert max_distance in (None,'np.inf','inf',), str(max_distance)
    if colored:
        depth, camjson, rgb = load_depth_and_camjson(depthfile, True)
        rgb_image = np.copy(rgb)
    else:
        depth, camjson = load_depth_and_camjson(depthfile, False)
    depth_image = np.copy(depth)
    screen_width = int(depth.shape[1])
    screen_height =int(depth.shape[0])
    if fov_degrees_vertical or fov_degrees_horizontal:
        fov_v, _ = fovv_and_fovh_degrees_given_either(fov_degrees_vertical, fov_degrees_horizontal, screen_width/screen_height)
    else:
        fov_v = fov_v_from_camjson(camjson, screen_width / screen_height)
    # fov_v = fov_v *1.5
    assert 'extrinsic_cam2world' in camjson, str(sorted(list(camjson.keys())))
    cam2world = np.float64(camjson['extrinsic_cam2world']).reshape((3,4))
    # cam2world[:3, 3] /= 2
    cam2world = np.pad(cam2world, ((0,1),(0,0)))
    cam2world[-1,-1] = 1.
    if transform_S is not None:
        cam2world = apply_transform_to_cam2world(cam2world, transform_S)
    # conversion_matrix = np.array([
    #     [1,  0,  0, 0],
    #     [0,  0,  1, 0],
    #     [0, -1,  0, 0],
    #     [0,  0,  0, 1]
    # ])
    
    # cam2world = cam2world @ conversion_matrix
    # cam2world = np.linalg.inv(cam2world)
    cam2screen = build_intrinsicmatrix_camtoscreenpix_pinhole_camera(fov_vertical_degrees=fov_v, \
        screen_width=screen_width, screen_height=screen_height)
    world2screen = np.matmul(cam2screen, np.linalg.pinv(cam2world))
    screen2world = np.linalg.pinv(world2screen)

    wpoints, imcoords = depth_image_to_4dscreencolumnvectors(depth)
    if colored:
        rgb = rgb.reshape((-1,3))

    if max_distance is not None and np.isfinite(max_distance):
        depth_mask_keep = np.less(depth, max_distance).flatten()
        wpoints = np.stack([wpoints[ii,:][depth_mask_keep] for ii in range(wpoints.shape[0])],axis=0)
        imcoords = np.stack([imcoords[:,ii][depth_mask_keep] for ii in range(imcoords.shape[1])],axis=1)
        if colored:
            rgb = np.stack([rgb[:,ii][depth_mask_keep] for ii in range(rgb.shape[1])],axis=1)

    wpoints = np.ascontiguousarray(np.matmul(screen2world, wpoints).transpose()[:,:3])
    # GTA -> Open3D 坐标系
    S1 = np.array([[1, 0,  0],
                [0, 1, 0],
                [0, 0,  1]], dtype=np.float64)
    S2 = np.array([[-1, 0,  0],
                [0, 1, 0],
                [0, 0,  1]], dtype=np.float64)        
    S3 = np.array([[1, 0,  0],
                [0, -1, 0],
                [0, 0,  1]], dtype=np.float64)   
    S4 = np.array([[-1, 0,  0],
                [0, -1, 0],
                [0, 0,  1]], dtype=np.float64)
    S5 = np.array([[0, 1,  0],
                [1, 0, 0],
                [0, 0,  1]], dtype=np.float64)
    S6 = np.array([[0, -1,  0],
                [1, 0, 0],
                [0, 0,  1]], dtype=np.float64)
    S7 = np.array([[0, 1,  0],
                [-1, 0, 0],
                [0, 0,  1]], dtype=np.float64)
    S8 = np.array([[0, -1,  0],
                [-1, 0, 0],
                [0, 0,  1]], dtype=np.float64)
    # S = np.array([[1, 0,  0],
    #             [0, 0, -1],
    #             [0, 1,  0]], dtype=np.float64)
    # wpoints = wpoints @ S8.T  # 行向量右乘 S^T
    if subsample_amt > 0: # quick and dirty random subsampling... voxel subsampling is nicer
        if colored:
            wpoints, imcoords, rgb = random_subsample(subsample_amt, wpoints, imcoords, rgb)
        else:
            wpoints, imcoords = random_subsample(subsample_amt, wpoints, imcoords)

    ret_ = {'worldpoints':wpoints, 'pixcoords':imcoords, 'world2screen':world2screen, \
        'screen_width':screen_width, 'screen_height':screen_height, 'depth_image':depth_image}
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

def generate_48_orientations():
    import itertools
    orientations = []
    # 所有坐标轴的排列（6种）
    for perm in itertools.permutations([0, 1, 2]):
        # 每个轴可以正负（但行列式必须为 ±1，总数控制在48）
        for sx in (-1, 1):
            for sy in (-1, 1):
                for sz in (-1, 1):
                    matrix = np.zeros((3,3), dtype=np.float64)
                    matrix[0, perm[0]] = sx
                    matrix[1, perm[1]] = sy
                    matrix[2, perm[2]] = sz
                    # 检查是否为正交矩阵且 det ∈ {±1}
                    if np.allclose(np.linalg.det(matrix), 1) or np.allclose(np.linalg.det(matrix), -1):
                        # 去重（浮点误差）
                        if not any(np.allclose(matrix, x, atol=1e-8) for x in orientations):
                            orientations.append(matrix.copy())
    assert len(orientations) == 48, f"Expected 48, got {len(orientations)}"
    return orientations

if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument("depth_files", nargs="+")
    parser.add_argument("-max", "--max_distance_clip_cloud", type=float, default=1e9)
    parser.add_argument("-ss", "--subsample_amt", type=int, default=0)
    parser.add_argument("-nc", "--no_color_avail", action="store_false", dest="color_avail")
    parser.add_argument("-fovv", "--fov_degrees_vertical", type=float, help="optional if already in camera meta json")
    parser.add_argument("-fovh", "--fov_degrees_horizontal", type=float)
    parser.add_argument("-o", "--save_to_file_prefix", type=str, default="")
    args = parser.parse_args()

    depth_files = files_glob(args.depth_files)
    if not depth_files:
        print("❌ 未找到匹配的深度文件。")
        exit(1)

    # 生成 48 种变换
    transformations = generate_48_orientations()
    print(f"🔄 共 {len(transformations)} 种变换将被尝试...")

    accepted_transforms = []

    for idx, S in enumerate(transformations):
        print(f"\n🔄 应用变换 #{idx+1}/48 到 cam2world 上:\n{S}")

        # 使用当前 S 变换，重新加载所有帧
        partial_loader = partial(
            load_cloud_via_depth_and_camjson,
            colored=args.color_avail,
            max_distance=args.max_distance_clip_cloud,
            subsample_amt=args.subsample_amt,
            fov_degrees_vertical=args.fov_degrees_vertical,
            fov_degrees_horizontal=args.fov_degrees_horizontal,
            transform_S=S,  # <<< 关键：注入变换矩阵 >>>
        )

        raw_clouds = process_map(partial_loader, depth_files, max_workers=None)

        valid_clouds = [c for c in raw_clouds if c is not None]
        if len(valid_clouds) == 0:
            print("⚠️ 该变换下无有效帧加载。跳过...")
            continue

        transformed_cloud = merge_clouds_world_points(valid_clouds)

        # 可视化
        print("🔍 正在显示变换后的点云... (关闭窗口以继续)")
        visualize_clouds(transformed_cloud)

        # 用户交互
        while True:
            choice = input("❓ 保留此变换吗？[y]es / [n]o / [q]uit: ").strip().lower()
            if choice in ['y', 'yes']:
                print("✅ 已接受该变换。")
                savefile = f"{args.save_to_file_prefix}_transform_{idx}.ply" if args.save_to_file_prefix else None
                accepted_transforms.append({'index': idx, 'matrix': S.tolist(), 'filename': savefile})
                if savefile:
                    save_cloud_to_file(transformed_cloud, savefile)
                    print(f"💾 已保存至: {savefile}")
                break
            elif choice in ['n', 'no']:
                print("⏭️ 跳过。")
                break
            elif choice in ['q', 'quit']:
                print("👋 退出程序。")
                break
            else:
                print("⚠️ 输入无效，请输入 yes/no/quit 或 y/n/q")
        
        if choice in ['q', 'quit']:
            break

    # 最终报告
    print("\n" + "="*50)
    if accepted_transforms:
        print("🎉 接受了以下变换:")
        for t in accepted_transforms:
            print(f"   变换 #{t['index']} -> 保存为: {t['filename']}")
    else:
        print("🚫 未接受任何变换。")
    print("="*50)

