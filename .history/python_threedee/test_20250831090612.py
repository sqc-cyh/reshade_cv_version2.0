# pip install open3d pillow numpy
import numpy as np
import open3d as o3d
from PIL import Image
import json

def build_K_from_fov(H, W, fov_v_deg):
    fy = (H / 2.0) / np.tan(np.deg2rad(fov_v_deg) / 2.0)
    fx = fy * (W / H)
    cx, cy = W / 2.0, H / 2.0
    K = np.array([[fx, 0,  cx],
                  [0,  fy, cy],
                  [0,  0,  1 ]], dtype=np.float64)
    return K

def depth_to_pcd(depth_m, rgb_u8, K, depth_is_z=True, stride=1, z_clip=(0.05, 100.0)):
    H, W = depth_m.shape
    fx, fy, cx, cy = K[0,0], K[1,1], K[0,2], K[1,2]
    vs = np.arange(0, H, stride)
    us = np.arange(0, W, stride)
    uu, vv = np.meshgrid(us, vs)
    d = depth_m[vv, uu]

    if z_clip is not None:
        zmin, zmax = z_clip
        mask = np.isfinite(d) & (d > zmin) & (d < zmax)
    else:
        mask = np.isfinite(d) & (d > 0)

    uu, vv, d = uu[mask], vv[mask], d[mask]

    if depth_is_z:
        z = d
        x = (uu - cx) * z / fx
        y = (vv - cy) * z / fy
    else:
        # 欧氏距离：先构建单位射线方向，再乘以距离
        xdir = (uu - cx) / fx
        ydir = (vv - cy) / fy
        inv_norm = 1.0 / np.sqrt(xdir**2 + ydir**2 + 1.0)
        x = xdir * d * inv_norm
        y = ydir * d * inv_norm
        z = 1.0   * d * inv_norm

    xyz = np.stack([x, y, z], axis=1)
    rgb = rgb_u8[vv, uu, :]

    pcd = o3d.geometry.PointCloud()
    pcd.points = o3d.utility.Vector3dVector(xyz.astype(np.float64))
    pcd.colors = o3d.utility.Vector3dVector((rgb.astype(np.float32) / 255.0))
    return pcd

def preprocess_pcd(pcd, voxel=0.01):
    p = pcd.voxel_down_sample(voxel)
    p.estimate_normals(
        o3d.geometry.KDTreeSearchParamHybrid(radius=voxel*5, max_nn=50)
    )
    p.orient_normals_consistent_tangent_plane(50)
    return p

def run_icp(source, target, max_voxel=0.05):
    # 金字塔 ICP：粗->细
    regs = [(max_voxel*4, 50), (max_voxel*2, 30), (max_voxel, 20)]
    trans = np.eye(4)
    criteria = o3d.pipelines.registration.ICPConvergenceCriteria(
        relative_fitness=1e-6, relative_rmse=1e-6, max_iteration=200
    )
    current_src = source
    for voxel, _ in regs:
        s = preprocess_pcd(current_src, voxel)
        t = preprocess_pcd(target, voxel)
        result = o3d.pipelines.registration.registration_icp(
            s, t, max_correspondence_distance=voxel*4,
            init=trans,
            estimation_method=o3d.pipelines.registration.TransformationEstimationPointToPlane(),
            criteria=criteria
        )
        trans = result.transformation
        current_src = current_src.transform(trans)  # 累积更新
    return current_src, trans, result

if __name__ == "__main__":
    # ---- 修改为你的路径 ----
    rgb1_path   = r"F:\SteamLibrary\steamapps\common\Grand Theft Auto V\cv_saved\GTAV_2025-08-30_207306338_RGB.png"
    depth1_path = "frame1_depth.npy"  # 单位：米
    meta1_path  = "frame1.json"       # 若无 fov，可手填
    rgb2_path   = "frame2_rgb.png"
    depth2_path = "frame2_depth.npy"
    meta2_path  = "frame2.json"

    rgb1 = np.asarray(Image.open(rgb1_path).convert("RGB"))
    rgb2 = np.asarray(Image.open(rgb2_path).convert("RGB"))
    depth1 = np.load(depth1_path).astype(np.float32)
    depth2 = np.load(depth2_path).astype(np.float32)
    H, W = depth1.shape

    # 1) 内参 K：尽量用 meta 的 fov_v_degrees；若没有可手填一个近似值（比如 60~75）
    fov1 = json.load(open(meta1_path)).get("fov_v_degrees", 69.0)
    fov2 = json.load(open(meta2_path)).get("fov_v_degrees", fov1)
    K1 = build_K_from_fov(H, W, fov1)
    K2 = build_K_from_fov(H, W, fov2)

    # 2) 仅用深度+RGB 回投影为相机坐标系点云（忽略任何 c2w）
    # 如果你的 .npy 是欧氏距离，把 depth_is_z=False
    pcd1 = depth_to_pcd(depth1, rgb1, K1, depth_is_z=True, stride=1, z_clip=(0.05, 200.0))
    pcd2 = depth_to_pcd(depth2, rgb2, K2, depth_is_z=True, stride=1, z_clip=(0.05, 200.0))

    # 3) ICP 配准（估计 1->2 的相对位姿）
    pcd2_aligned, T12, reg = run_icp(pcd1, pcd2, max_voxel=0.03)
    print("Estimated T_1_to_2 =\n", T12)
    print(f"ICP fitness={reg.fitness:.4f}, inlier_rmse={reg.inlier_rmse:.4f}")

    # 4) 融合 & 可视化
    merged = pcd2 + pcd2_aligned
    o3d.io.write_point_cloud("depth_only_icp_merged.ply", merged)
    print("Saved depth_only_icp_merged.ply")
    # o3d.visualization.draw_geometries([merged])  # 需要交互时再打开
