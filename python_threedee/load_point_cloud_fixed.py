#!/usr/bin/env python3
import os
import argparse
import json
import math
import numpy as np
from PIL import Image
from save_point_cloud_to_file import save_cloud_to_file
from misc_utils import files_glob
from functools import partial
from tqdm.contrib.concurrent import process_map

# -------------------------- 核心工具函数（与正确脚本对齐） --------------------------
def d2r(x):
    return x * math.pi / 180.0

def make_K_from_fovy(fovy_deg, W, H, aspect_ratio=None):
    """用垂直FOV计算内参（与正确脚本逻辑一致）"""
    if aspect_ratio is None:
        aspect_ratio = W / H
    fovy = d2r(fovy_deg)
    fy = (H * 0.5) / math.tan(fovy * 0.5)
    fovx = 2.0 * math.atan(aspect_ratio * math.tan(fovy * 0.5))  # 转换为水平FOV
    fx = (W * 0.5) / math.tan(fovx * 0.5)
    cx = (W - 1) / 2.0
    cy = (H - 1) / 2.0
    return fx, fy, cx, cy

def backproject_points_from_z_depth(depth, fx, fy, cx, cy, stride=1):
    """像素→相机系反投影（与正确脚本一致）"""
    H, W = depth.shape[:2]
    us = np.arange(0, W, stride)
    vs = np.arange(0, H, stride)
    uu, vv = np.meshgrid(us, vs)
    
    z = depth[::stride, ::stride]
    x = (uu - cx) * z / fx
    y = (vv - cy) * z / fy
    
    pts_cam = np.stack([x, -y, -z], axis=-1).reshape(-1, 3)
    return pts_cam, uu.reshape(-1), vv.reshape(-1)

# -------------------------- 从extrinsic_cam2world解析UE→OpenCV转换 --------------------------
# def cam2world_to_cv(cam2world, pose_scale=1.0):
#     """
#     将UE的3x4相机矩阵转换为OpenCV系c2w矩阵
#     cam2world: 3x4数组，格式为[R_ue(3x3) | t_ue(3x1)]
#     """
#     # 1. 提取UE系旋转和平移
#     R_ue = cam2world[:, :3]  # 3x3旋转矩阵（UE系）
#     t_ue = cam2world[:, 3]   # 3x1平移向量（UE系，未缩放）
    
#     # 2. 应用缩放（与正确脚本的pose_scale一致）
#     t_scaled = t_ue * pose_scale
    
#     # 3. UE→OpenCV轴映射（与正确脚本的M_to_CV一致）
#     M_to_CV = np.array([[0, 1,  0],
#                            [0, 0, -1],
#                            [1, 0,  0]], dtype=np.float64)
#     # 旋转矩阵转换
#     R_cv = M_to_CV @ R_ue @ M_to_CV.T
#     # 平移向量转换（分量对应与正确脚本一致）
#     t_cv = np.array([
#         t_scaled[2],  # OpenCV X = UE X（前方向）
#         t_scaled[1],  # OpenCV Y = UE Z（上方向）
#         t_scaled[0]   # OpenCV Z = UE Y（右方向）
#     ], dtype=np.float64)
    
#     # 4. 构造4x4 c2w矩阵
#     c2w = np.eye(4, dtype=np.float64)
#     c2w[:3, :3] = R_cv
#     c2w[:3, 3] = t_cv
#     return c2w, R_cv, t_cv

def cam2world_to_cv_unchanged(cam2world, pose_scale=1.0):
    """
    将UE的3x4相机矩阵转换为OpenCV系c2w矩阵
    cam2world: 3x4数组，格式为[R_ue(3x3) | t_ue(3x1)]
    """
    # 1. 提取UE系旋转和平移
    R_cv = cam2world[:, :3]  # 3x3旋转矩阵（UE系）
    t_cv = cam2world[:, 3]   # 3x1平移向量（UE系，未缩放）
    
    # 4. 构造4x4 c2w矩阵
    c2w = np.eye(4, dtype=np.float64)
    c2w[:3, :3] = R_cv
    c2w[:3, 3] = t_cv
    return c2w, R_cv, t_cv

# -------------------------- 加载深度和相机文件（优先camera.json，再找meta.json） --------------------------
def load_depth_and_meta(depthfile:str, and_rgb:bool):
    """适配逻辑：优先查找camera.json，不存在则查找meta.json，均需含extrinsic_cam2world和fov_v_degrees"""
    # 1. 解析深度文件
    if depthfile.endswith('_depth.npy'):
        depthbnam = depthfile[:-len('_depth.npy')]
        depth = np.load(depthfile, allow_pickle=False)
    elif depthfile.endswith('_depth.fpzip'):
        depthbnam = depthfile[:-len('_depth.fpzip')]
        import fpzip
        with open(depthfile,'rb') as infile:
            depth = fpzip.decompress(infile.read())
        if len(depth.shape) == 4:
            depth = depth[0,0,:,:]
    else:
        print(f"[警告] 不支持的深度格式: {depthfile}")
        return None
    
    assert depth.dtype in (np.float32, np.float64), f"深度数据类型错误: {depth.dtype}"
    assert len(depth.shape) == 2, f"深度维度错误: {depth.shape}"

    # 2. 优先查找camera.json
    cam_file = depthbnam + '_camera.json'
    if os.path.isfile(cam_file):
        print(f"[DEBUG] 找到camera.json: {cam_file}")
        with open(cam_file,'r') as f:
            cam_data = json.load(f)
    else:
        # camera.json不存在，查找meta.json
        print(f"[DEBUG] 未找到camera.json: {cam_file}，尝试查找meta.json")
        cam_file = depthbnam + '_meta.json'
        if not os.path.isfile(cam_file):
            print(f"[警告] 未找到camera.json和meta.json: {depthbnam}_camera.json / {depthbnam}_meta.json")
            return None
        print(f"[DEBUG] 找到meta.json: {cam_file}")
        with open(cam_file,'r') as f:
            cam_data = json.load(f)
    
    # 3. 验证相机文件必要字段（两种文件统一验证标准）
    required_keys = ['extrinsic_cam2world', 'fov_v_degrees']
    for k in required_keys:
        if k not in cam_data:
            print(f"[警告] {os.path.basename(cam_file)}缺少字段'{k}': {sorted(cam_data.keys())}")
            return None

    # 4. 读取RGB
    if and_rgb:
        rgb_file = depthbnam + '_RGB.png'
        if not os.path.isfile(rgb_file):
            print(f"[警告] 未找到RGB文件: {rgb_file}")
            return None
        rgb = np.asarray(Image.open(rgb_file).convert('RGB'))
        assert rgb.shape[:2] == depth.shape[:2], f"RGB与深度尺寸不匹配: {rgb.shape} vs {depth.shape}"
        return depth, cam_data, rgb, depthbnam
    return depth, cam_data, None, depthbnam

def linearize_depth(z_depth, z_near=0.3, z_far=1000.0, reversed=True):
    """Convert ReShade raw depth [0,1] to linear metric depth."""
    if reversed:
        return (z_near * z_far) / (z_far - z_depth * (z_far - z_near))
    else:
        return (2.0 * z_near * z_far) / (z_far + z_near - (2.0 * z_depth - 1.0) * (z_far - z_near))


# -------------------------- 生成点云（核心逻辑不变） --------------------------
def load_cloud_via_meta(depthfile:str,
            colored:bool,
            max_distance:float=None,
            subsample_amt:int=0,
            pose_scale:float=1.0,
            ):
    # 加载深度和相机数据（优先camera.json）
    result = load_depth_and_meta(depthfile, colored)
    if result is None:
        return None
    if colored:
        depth, cam_data, rgb, depthbnam = result
        rgb_image = np.copy(rgb)
    else:
        depth, cam_data, _, depthbnam = result
    depth = np.flip(depth, axis=0)
    # depth = linearize_depth(depth, reversed=True)
    depth_image = np.copy(depth)
    H, W = depth.shape[:2]
    aspect_ratio = W / H

    # 1. 从相机数据读取参数（camera.json和meta.json结构一致）
    fov_v_deg = float(cam_data['fov_v_degrees'])  # 垂直FOV
    cam2world = np.array(cam_data['extrinsic_cam2world'], dtype=np.float64).reshape(3, 4)  # 3x4相机矩阵
    print("cam2world:\n", cam2world)
    # 2. 转换为OpenCV系c2w矩阵（与正确脚本对齐）
    c2w, R_cv, t_cv = cam2world_to_cv_unchanged(cam2world, pose_scale)
    print(f"[DEBUG] 帧 {depthbnam} 的c2w矩阵:\n{c2w}")

    # 3. 计算内参（用垂直FOV，与正确脚本逻辑一致）
    fx, fy, cx, cy = make_K_from_fovy(fov_v_deg, W, H, aspect_ratio)
    print(f"[DEBUG] 内参: fx={fx:.2f}, fy={fy:.2f}, cx={cx:.2f}, cy={cy:.2f}")

    # 4. 点云反投影
    pts_cam, uu, vv = backproject_points_from_z_depth(depth, fx, fy, cx, cy, stride=1)
    # 深度裁剪（与正确脚本一致）
    depth_flat = depth[vv, uu]
    depth_mask_keep = (depth_flat >= 0.2) & (depth_flat <= max_distance)
    pts_cam = pts_cam[depth_mask_keep]
    uu_keep = uu[depth_mask_keep]
    vv_keep = vv[depth_mask_keep]
    if colored:
        rgb_keep = rgb[vv_keep, uu_keep]

    # 5. 相机系→世界系
    pts_world = (R_cv @ pts_cam.T).T + t_cv[None, :]

    # 6. 下采样
    if subsample_amt > 0:
        perm = np.random.permutation(len(pts_world))[::subsample_amt]
        pts_world = pts_world[perm]
        uu_keep = uu_keep[perm]
        vv_keep = vv_keep[perm]
        if colored:
            rgb_keep = rgb_keep[perm]

    # 组装结果
    ret = {
        'worldpoints': pts_world,
        'pixcoords': np.stack([uu_keep, vv_keep], axis=1),
        'depth_image': depth_image,
        'screen_width': W,
        'screen_height': H,
        'c2w': c2w
    }
    if colored:
        ret['colors'] = rgb_keep
        ret['rgb_image'] = rgb_image
    return ret

# -------------------------- 合并与可视化 --------------------------
def merge_clouds_world_points(clouds):
    if isinstance(clouds, dict):
        return clouds
    mergeable = ['worldpoints']
    if all(['colors' in cl for cl in clouds]):
        mergeable.append('colors')
    merged = {k: [] for k in mergeable}
    for cl in clouds:
        for k in mergeable:
            merged[k].append(cl[k])
    return {k: np.concatenate(v, axis=0) for k, v in merged.items()}

def visualize_clouds(clouds):
    import open3d
    if isinstance(clouds, dict):
        clouds = [clouds]
    o3dcloud = open3d.geometry.PointCloud()
    o3dcloud.points = open3d.utility.Vector3dVector(np.concatenate([c['worldpoints'] for c in clouds]))
    if 'colors' in clouds[0]:
        colors = []
        for c in clouds:
            if c['colors'].dtype == np.uint8:
                colors.append(np.float32(c['colors']) / 255.)
            else:
                colors.append(c['colors'])
        o3dcloud.colors = open3d.utility.Vector3dVector(np.concatenate(colors))
    open3d.visualization.draw([o3dcloud])


def add_camera_global_axis(merged_cloud, valid_clouds):
    # hyperparameter
    # N_global = 100 # number of points for global XYZ
    max_global = 100
    N_camera = 2000 # number of points for camera xyz
    max_camera = 10

    # visualize the global XYZ
    # global_x = np.zeros((N_global, 3))
    # global_x[:,0] = np.linspace(0, max_global, N_global)
    # global_x_color = np.zeros(global_x.shape)
    # global_x_color[:,0] = 1

    # global_y = np.zeros((N_global, 3))
    # global_y[:,1] = np.linspace(0, max_global, N_global)
    # global_y_color = np.zeros(global_y.shape)
    # global_y_color[:,1] = 1

    # global_z = np.zeros((N_global, 3))
    # global_z[:,2] = np.linspace(0, max_global, N_global)
    # global_z_color = np.zeros(global_z.shape)
    # global_z_color[:,2] = 1

    c2ws = []
    for valid_cloud in valid_clouds:
        c2ws.append(valid_cloud['c2w'][:3, :4])
    c2ws = np.array(c2ws)

    # visualize the camera xyz
    camera_centers = c2ws[:,:3,3]
    camera_centers_color = np.zeros(camera_centers.shape)

    camera_xs = np.linspace(0, max_camera, N_camera).reshape(N_camera, 1, 1)
    camera_x_dirs = c2ws[:,:3,0]
    camera_x_dirs = camera_x_dirs.reshape(1, *camera_x_dirs.shape)
    camera_xs = camera_xs * camera_x_dirs + camera_centers[None]
    camera_xs = camera_xs.reshape(-1, 3)
    camera_xs_color = np.zeros(camera_xs.shape)
    camera_xs_color[:,0] = 255

    camera_ys = np.linspace(0, max_camera, N_camera).reshape(N_camera, 1, 1)
    camera_y_dirs = c2ws[:,:3,1]
    camera_y_dirs = camera_y_dirs.reshape(1, *camera_y_dirs.shape)
    camera_ys = camera_ys * camera_y_dirs + camera_centers[None]
    camera_ys = camera_ys.reshape(-1, 3)
    camera_ys_color = np.zeros(camera_ys.shape)
    camera_ys_color[:,1] = 255

    camera_zs = np.linspace(0, max_camera, N_camera).reshape(N_camera, 1, 1)
    camera_z_dirs = c2ws[:,:3,2]
    camera_z_dirs = camera_z_dirs.reshape(1, *camera_z_dirs.shape)
    camera_zs = camera_zs * camera_z_dirs + camera_centers[None]
    camera_zs = camera_zs.reshape(-1, 3)
    camera_zs_color = np.zeros(camera_zs.shape)
    camera_zs_color[:,2] = 255

    # plots
    pts = np.concatenate([
        camera_centers,
        # global_x, global_y, global_z, 
        camera_xs, camera_ys, 
        camera_zs,
    ], axis=0)
    colors = np.concatenate([
        camera_centers_color,
        # global_x_color, global_y_color, global_z_color, 
        camera_xs_color, camera_ys_color, 
        camera_zs_color,
    ], axis=0).astype(np.uint8)
    merged_cloud['worldpoints'] = np.concatenate([merged_cloud['worldpoints'], pts])
    merged_cloud['colors'] = np.concatenate([merged_cloud['colors'], colors])

# -------------------------- 主函数 --------------------------
if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument("depth_files", nargs="+")
    parser.add_argument("-max", "--max_distance_clip_cloud", type=float, default=50.0)
    parser.add_argument("-ss", "--subsample_amt", type=int, default=3)
    parser.add_argument("-nc", "--no_color_avail", action="store_false", dest="color_avail")
    parser.add_argument("-scale", "--pose_scale", type=float, default=1.0, help="相机位置缩放因子")
    parser.add_argument("-o", "--save_to_file", type=str, default="output.ply")
    args = parser.parse_args()

    args.depth_files = files_glob(args.depth_files)
    if not args.depth_files:
        print("❌ 未找到深度文件")
        exit(1)

    raw_clouds = process_map(
        partial(load_cloud_via_meta,
                colored=args.color_avail,
                max_distance=args.max_distance_clip_cloud,
                subsample_amt=args.subsample_amt,
                pose_scale=args.pose_scale),
        args.depth_files
    )
    # import pdb; pdb.set_trace()

    valid_clouds = [c for c in raw_clouds if c is not None]
    if len(valid_clouds) == 0:
        print("❌ 未加载到有效点云")
        exit(1)

    print(f"✅ 加载{len(valid_clouds)}帧有效点云，合并中...")
    merged_cloud = merge_clouds_world_points(valid_clouds)
    if args.save_to_file:
        # save_cloud_to_file(merged_cloud, args.save_to_file)
        print(f"💾 点云已保存至: {args.save_to_file}")
    
    add_camera_global_axis(merged_cloud, valid_clouds)


    
    
    
    
    
    
    visualize_clouds(merged_cloud)