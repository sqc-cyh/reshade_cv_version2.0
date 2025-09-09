# analyze_depth.py
import json, numpy as np

def load(depth_path):
    root = depth_path[:-len('_depth.npy')]
    D  = np.load(depth_path)
    MJ = json.load(open(root+'_meta.json','r'))
    return D, MJ

def K_from_fov(W,H,fov_v=None,fov_h=None, halfpx=True):
    if fov_v is None and fov_h is not None:
        # 水平FOV->垂直FOV
        fov_v = 2*np.degrees(np.arctan(np.tan(np.radians(fov_h)/2.) * (H/float(W))))
    f = 0.5*H/np.tan(np.radians(fov_v)/2.)
    fx = f; fy = f
    cx, cy = ((W-1)/2., (H-1)/2.) if halfpx else (W/2., H/2.)
    return fx,fy,cx,cy

def rays_grid(W,H,fx,fy,cx,cy, flip_y=True, halfpx=True):
    u,v = np.meshgrid(np.arange(W), np.arange(H))
    if halfpx:
        u = u + 0.5
        v = v + 0.5
    x = (u - cx) / fx
    y = (v - cy) / fy
    if flip_y:
        y = -y
    r = np.stack([x,y,np.ones_like(x)], -1)
    r = r / np.linalg.norm(r, axis=-1, keepdims=True)
    return r  # H×W×3

def rays_from_uv(u,v,fx,fy,cx,cy, flip_y=True):
    # u,v 为浮点像素坐标（不再强加 0.5）
    x = (u - cx) / fx
    y = (v - cy) / fy
    if flip_y:
        y = -y
    r = np.stack([x,y,np.ones_like(x)], -1)
    r = r / np.linalg.norm(r, axis=-1, keepdims=True)
    return r  # N×3

def build_c2w(M3x4, mode='c2w', transpose_R=False):
    M = np.eye(4, dtype=np.float64)
    M[:3,:4] = np.array(M3x4, dtype=np.float64).reshape(3,4)
    if transpose_R:
        R = M[:3,:3].T
        t = M[:3,3].copy()
        M[:3,:3] = R
        M[:3,3]  = t
    if mode.lower() in ('w2c','world2cam'):
        M = np.linalg.inv(M)
    return M  # 4×4 cam->world

def bilinear_sample(D, u, v):
    H,W = D.shape
    u = np.asarray(u); v = np.asarray(v)
    u0 = np.floor(u).astype(np.int32); v0 = np.floor(v).astype(np.int32)
    u1 = (u0 + 1).clip(0, W-1); v1 = (v0 + 1).clip(0, H-1)
    u0 = u0.clip(0, W-1); v0 = v0.clip(0, H-1)
    du = (u - u0); dv = (v - v0)
    Ia = D[v0, u0]; Ib = D[v0, u1]; Ic = D[v1, u0]; Id = D[v1, u1]
    return (Ia*(1-du)*(1-dv) + Ib*du*(1-dv) + Ic*(1-du)*dv + Id*du*dv)

def reproject_rms(depth_i, meta_i, depth_j, meta_j,
                  mode='c2w', transpose_R=False, use_hfov=False,
                  step=64, max_depth=np.inf, flip_y=True):
    H,W = depth_i.shape
    # i 帧内参与射线
    fov_v_i = meta_i.get('fov_v_degrees', None)
    fov_h_i = meta_i.get('fov_h_degrees', None) if use_hfov else None
    fx_i, fy_i, cx_i, cy_i = K_from_fov(W, H, fov_v=fov_v_i, fov_h=fov_h_i, halfpx=True)
    Rhat_i = rays_grid(W, H, fx_i, fy_i, cx_i, cy_i, flip_y=flip_y, halfpx=True)

    # 选子采样像素
    m_valid = np.isfinite(depth_i) & (depth_i > 0) & (depth_i < max_depth)
    idxs = np.flatnonzero(m_valid.ravel())[::step]
    if idxs.size == 0:
        return np.inf

    # i: cam 点
    t_i = depth_i.ravel()[idxs]
    rays_i = Rhat_i.reshape(-1,3)[idxs]
    Pcam_i = t_i[:,None] * rays_i                         # N×3
    Pcam_i_h = np.c_[Pcam_i, np.ones((Pcam_i.shape[0],1))]  # N×4

    # 外参：i 的 cam->world，j 的 world->cam
    C2W_i = build_c2w(meta_i['extrinsic_cam2world'], mode, transpose_R)  # 4×4
    C2W_j = build_c2w(meta_j['extrinsic_cam2world'], mode, transpose_R)  # 4×4
    W2C_j = np.linalg.inv(C2W_j)

    # 变换到 j 的相机系（注意列向量）
    Xw = (C2W_i @ Pcam_i_h.T)          # 4×N
    Xc_j = (W2C_j @ Xw).T[:, :3]       # N×3
    z = np.clip(Xc_j[:,2], 1e-6, None)

    # j 帧内参，投到 j 图像
    fov_v_j = meta_j.get('fov_v_degrees', None)
    fov_h_j = meta_j.get('fov_h_degrees', None) if use_hfov else None
    fx_j, fy_j, cx_j, cy_j = K_from_fov(W, H, fov_v=fov_v_j, fov_h=fov_h_j, halfpx=True)
    u_proj = fx_j * (Xc_j[:,0]/z) + cx_j
    v_proj = fy_j * (Xc_j[:,1]/z) + cy_j    # 与 rays 的 y 翻转保持同一约定

    # 在 j 的深度图上双线性采样，并在 j 帧重建 3D（欧氏深度假设）
    t_j = bilinear_sample(depth_j, u_proj, v_proj)
    valid = np.isfinite(t_j) & (t_j > 0) & (u_proj>=0) & (u_proj<=W-1) & (v_proj>=0) & (v_proj<=H-1)
    if valid.sum() < 10:
        return np.inf

    rays_j = rays_from_uv(u_proj[valid], v_proj[valid], fx_j, fy_j, cx_j, cy_j, flip_y=flip_y)
    Pcam_j_est = t_j[valid][:,None] * rays_j           # N'×3

    # 与几何投过去的 Xc_j 对比
    resid = np.linalg.norm(Pcam_j_est - Xc_j[valid], axis=1)
    return float(np.sqrt(np.mean(resid**2)))
    

if __name__ == "__main__":
    # 替换为你的两帧路径
    Di,Mi = load(r'F:\SteamLibrary\steamapps\common\Grand Theft Auto V\cv_saved\GTAV_2025-08-30_2057837496_depth.npy')
    Dj,Mj = load(r'F:\SteamLibrary\steamapps\common\Grand Theft Auto V\cv_saved\GTAV_2025-08-30_216898244_depth.npy')

    for mode in ['c2w','w2c']:
        for tr in [False, True]:
            for use_h in [False, True]:
                rms = reproject_rms(Di,Mi,Dj,Mj, mode=mode, transpose_R=tr, use_hfov=use_h,
                                    step=64, max_depth=np.inf, flip_y=True)
                tag = f"{mode} | {'R^T' if tr else 'R'} | {'hFOV' if use_h else 'vFOV'}"
                print(f"{tag:18s}  RMS_3D = {rms:.4f} m")
