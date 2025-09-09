import json, argparse, numpy as np
from PIL import Image

def load_cam2world(meta_json_path):
    with open(meta_json_path, "r") as f:
        meta = json.load(f)
    fov_v_deg = float(meta["fov_v_degrees"])
    M = np.array(meta["extrinsic_cam2world"], dtype=np.float64).reshape(3,4)
    # 3x4 -> 4x4 齐次
    T_cw = np.eye(4, dtype=np.float64)
    T_cw[:3,:3] = M[:,:3]
    T_cw[:3, 3] = M[:, 3]
    return T_cw, fov_v_deg

def intrinsics_from_fovv(fov_v_deg, W, H):
    # 针孔模型，垂直 FOV
    fy = H / (2.0 * np.tan(np.deg2rad(fov_v_deg) / 2.0))
    fx = fy * (W / H)
    cx, cy = (W - 1) / 2.0, (H - 1) / 2.0
    K = np.array([[fx, 0,  cx],
                  [0,  fy, cy],
                  [0,  0,   1]], dtype=np.float64)
    return K

def reprojection_check(rgb_path, depth_path, meta_path, stride=4, z_min=1e-4, z_max=1e6):
    # 读取
    rgb  = np.asarray(Image.open(rgb_path).convert("RGB"))
    depth = np.load(depth_path).astype(np.float64)   # 假设已是“米”
    H, W = depth.shape

    T_cw, fov_v = load_cam2world(meta_path)
    K = intrinsics_from_fovv(fov_v, W, H)
    T_wc = np.linalg.inv(T_cw)   # world->cam

    # 采样像素网格
    us = np.arange(0, W, stride)
    vs = np.arange(0, H, stride)
    uu, vv = np.meshgrid(us, vs)
    z  = depth[vv, uu]
    mask = (z > z_min) & (z < z_max)

    uu = uu[mask].astype(np.float64)
    vv = vv[mask].astype(np.float64)
    z  =  z[mask]

    # 反投影到相机坐标: Xc = (u-cx)/fx * z, Yc = (v-cy)/fy * z, Zc = z
    fx, fy = K[0,0], K[1,1]
    cx, cy = K[0,2], K[1,2]
    Xc = (uu - cx) / fx * z
    Yc = (vv - cy) / fy * z
    Zc = z
    Pc = np.stack([Xc, Yc, Zc, np.ones_like(Zc)], axis=0)  # 4xN

    # 相机→世界→再投回同一帧（应当几乎无误差）
    Pw = (T_cw @ Pc)                     # 世界
    Pc2 = (T_wc @ Pw)                    # 回到相机（应≈原 Pc）
    x, y, z2 = Pc2[0,:], Pc2[1,:], Pc2[2,:]

    # 投影：u' = fx*x/z + cx
    up = fx * (x / z2) + cx
    vp = fy * (y / z2) + cy

    # 像素误差
    err = np.sqrt((up - uu)**2 + (vp - vv)**2)
    mean_e = float(np.mean(err))
    p90_e  = float(np.percentile(err, 90))
    max_e  = float(np.max(err))

    # 旋转矩阵正交性与行列式
    R = T_cw[:3,:3]
    ortho_err = np.linalg.norm(R @ R.T - np.eye(3))
    detR = np.linalg.det(R)

    print(f"[Reproj] Mean={mean_e:.3f}px  P90={p90_e:.3f}px  Max={max_e:.3f}px   (N={err.size})")
    print(f"[Rot] ||R R^T - I||_F = {ortho_err:.3e}, det(R) = {detR:.5f}")
    return mean_e, p90_e, max_e

if __name__ == "__main__":
    ap = argparse.ArgumentParser()
    ap.add_argument("--rgb", required=True)
    ap.add_argument("--depth", required=True)
    ap.add_argument("--meta", required=True)
    ap.add_argument("--stride", type=int, default=4)
    args = ap.parse_args()
    reprojection_check(args.rgb, args.depth, args.meta, stride=args.stride)
