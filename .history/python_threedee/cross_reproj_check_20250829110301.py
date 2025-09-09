import json, argparse, numpy as np
from PIL import Image

def load_meta(meta_path):
    with open(meta_path, "r") as f:
        m = json.load(f)
    T = np.array(m["extrinsic_cam2world"], dtype=np.float64).reshape(3,4)
    T_cw = np.eye(4); T_cw[:3,:3] = T[:,:3]; T_cw[:3,3] = T[:,3]
    fov_v = float(m["fov_v_degrees"])
    return T_cw, fov_v

def intrinsics_from_fovv(fov_v_deg, W, H):
    fy = H / (2.0 * np.tan(np.deg2rad(fov_v_deg) / 2.0))
    fx = fy * (W / H)
    cx, cy = (W - 1) / 2.0, (H - 1) / 2.0
    K = np.array([[fx, 0,  cx],
                  [0,  fy, cy],
                  [0,  0,   1]], dtype=np.float64)
    return K

def backproject(depth, K, stride=4, zmin=1e-4, zmax=1e6, depth_kind="Z"):
    H, W = depth.shape
    fx, fy, cx, cy = K[0,0], K[1,1], K[0,2], K[1,2]
    us = np.arange(0, W, stride); vs = np.arange(0, H, stride)
    uu, vv = np.meshgrid(us, vs)
    d = depth[vv, uu].astype(np.float64)

    gx = (uu - cx) / fx
    gy = (vv - cy) / fy
    g  = np.sqrt(1.0 + gx*gx + gy*gy)

    if depth_kind.lower() == "ray":
        Z = d / g         # 把 r 转回 Z
    else:
        Z = d             # 已是 Z

    m = np.isfinite(Z) & (Z > zmin) & (Z < zmax)
    uu, vv, Z, gx, gy = uu[m].astype(np.float64), vv[m].astype(np.float64), Z[m], gx[m], gy[m]
    X = gx * Z
    Y = gy * Z
    Pc = np.stack([X, Y, Z, np.ones_like(Z)], axis=0)
    return Pc, uu, vv, Z

def project(K, Pc):  # Pc: 4xN (camera coords)
    x, y, z = Pc[0,:], Pc[1,:], Pc[2,:]
    u = K[0,0] * (x / z) + K[0,2]
    v = K[1,1] * (y / z) + K[1,2]
    return u, v, z

def sample_depth_nn(depth, u, v):
    H, W = depth.shape
    ui = np.rint(u).astype(int)
    vi = np.rint(v).astype(int)
    m = (ui >= 0) & (ui < W) & (vi >= 0) & (vi < H)
    zq = np.full_like(u, np.nan, dtype=np.float64)
    zq[m] = depth[vi[m], ui[m]]
    return zq, m

def cross_check(rgbA, depthA, metaA, rgbB, depthB, metaB, stride=4, dz_rel_thr=0.05, dz_abs_thr=0.05):
    # 读取参数
    imA = np.asarray(Image.open(rgbA).convert("RGB"))
    imB = np.asarray(Image.open(rgbB).convert("RGB"))
    ZA = np.load(depthA).astype(np.float64)   # 假设已是“米”
    ZB = np.load(depthB).astype(np.float64)

    assert ZA.shape == ZB.shape, f"Depth size mismatch: {ZA.shape} vs {ZB.shape}"
    H, W = ZA.shape

    Tcw_A, fov_v_A = load_meta(metaA)
    Tcw_B, fov_v_B = load_meta(metaB)
    KA = intrinsics_from_fovv(fov_v_A, W, H)
    KB = intrinsics_from_fovv(fov_v_B, W, H)
    Twc_A, Twc_B = np.linalg.inv(Tcw_A), np.linalg.inv(Tcw_B)

    # A: 反投影（相机A坐标）
    PcA, uA, vA, zA = backproject(ZA, KA, stride=stride)

    # A->世界->B相机
    Pw  = Tcw_A @ PcA
    PcB = Twc_B @ Pw    # 4xN in camera-B
    uB, vB, zB_pred = project(KB, PcB)  # 投影到B像素 & 预测B相机系下深度(=Z)

    # 与 B 深度图一致性（最近邻）
    zB_obs, inimg = sample_depth_nn(ZB, uB, vB)
    valid = inimg & np.isfinite(zB_obs) & (zB_obs > 1e-4)

    # 统计相对/绝对Z误差
    dz = zB_pred[valid] - zB_obs[valid]
    rel = np.abs(dz) / np.maximum(zB_obs[valid], 1e-6)
    abs_ = np.abs(dz)

    if valid.sum() == 0:
        print("No valid projected points fall inside image B.")
        return

    inlier = (rel < dz_rel_thr) | (abs_ < dz_abs_thr)

    def q(x,p): return float(np.percentile(x, p)) if x.size else float("nan")

    print(f"[Cross] samples={PcA.shape[1]}  insideB={valid.sum()}  inlier={(inlier.sum())} ({inlier.sum()/valid.sum():.2%})")
    print(f"[dz_abs] mean={float(abs_.mean()):.4f} m  med={q(abs_,50):.4f}  p90={q(abs_,90):.4f}  p99={q(abs_,99):.4f}")
    print(f"[dz_rel] mean={float(rel.mean()):.4f}    med={q(rel,50):.4f}   p90={q(rel,90):.4f}   p99={q(rel,99):.4f}")

    # 可选：像素重投影误差仅用于直观看分布（与B像素“真值”没有配对基准）
    # 这里报告投影坐标是否合理落在图像内即可
    print(f"[Pix] uB range: [{float(np.nanmin(uB)): .1f}, {float(np.nanmax(uB)): .1f}], vB range: [{float(np.nanmin(vB)): .1f}, {float(np.nanmax(vB)): .1f}]")

if __name__ == "__main__":
    ap = argparse.ArgumentParser()
    ap.add_argument("--rgbA", required=True); ap.add_argument("--depthA", required=True); ap.add_argument("--metaA", required=True)
    ap.add_argument("--rgbB", required=True); ap.add_argument("--depthB", required=True); ap.add_argument("--metaB", required=True)
    ap.add_argument("--stride", type=int, default=4)
    ap.add_argument("--dz_rel_thr", type=float, default=0.05)  # 5% 相对阈值
    ap.add_argument("--dz_abs_thr", type=float, default=0.05)  # 5cm 绝对阈值
    ap.add_argument("--depth_kind", choices=["Z","ray"], default="Z")
    args = ap.parse_args()
    cross_check(args.rgbA, args.depthA, args.metaA, args.rgbB, args.depthB, args.metaB,
                stride=args.stride, dz_rel_thr=args.dz_rel_thr, dz_abs_thr=args.dz_abs_thr)
