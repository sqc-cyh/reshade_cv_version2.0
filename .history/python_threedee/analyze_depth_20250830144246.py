import json, numpy as np
from PIL import Image

def load(depth_path):
    root = depth_path[:-len('_depth.npy')]
    D  = np.load(depth_path)
    MJ = json.load(open(root+'_meta.json','r'))
    return D, MJ

def K_from_fov(W,H,fov_v=None,fov_h=None, halfpx=True):
    if fov_v is None and fov_h is not None:
        fov_v = 2*np.degrees(np.arctan(np.tan(np.radians(fov_h)/2.) * (H/W)))
    f = 0.5*H/np.tan(np.radians(fov_v)/2.)
    fx = f; fy = f
    cx, cy = ((W-1)/2., (H-1)/2.) if halfpx else (W/2., H/2.)
    return fx,fy,cx,cy

def rays(W,H,fx,fy,cx,cy, flip_y=True, halfpx=True):
    u,v = np.meshgrid(np.arange(W), np.arange(H))
    if halfpx: u=u+0.5; v=v+0.5
    x=(u-cx)/fx; y=(v-cy)/fy; 
    if flip_y: y=-y
    r = np.stack([x,y,np.ones_like(x)],-1)
    n = np.linalg.norm(r,axis=-1,keepdims=True)
    return r/n  # unit rays

def build_c2w(M3x4, mode='c2w', transpose_R=False):
    M=np.eye(4); M[:3,:4]=M3x4
    if transpose_R: 
        R = M[:3,:3].T; t = M[:3,3].copy()
        M[:3,:3]=R; M[:3,3]=t
    if mode=='w2c': 
        M = np.linalg.inv(M)
    return M

def reproject_rms(depth_i, meta_i, depth_j, meta_j, 
                  mode='c2w', transpose_R=False, use_hfov=False):
    H,W = depth_i.shape
    fov_v = meta_i.get('fov_v_degrees', None)
    fov_h = meta_i.get('fov_h_degrees', None) if use_hfov else None
    fx,fy,cx,cy = K_from_fov(W,H,fov_v=fov_v, fov_h=fov_h, halfpx=True)
    Rhat = rays(W,H,fx,fy,cx,cy, flip_y=True, halfpx=True)
    t = depth_i  # 欧氏距离
    Pcam = (t[...,None]*Rhat).reshape(-1,3)
    Pcam = Pcam[::64]  # 子采样
    Pcam_h = np.c_[Pcam, np.ones((len(Pcam),1))]
    C2W_i = build_c2w(np.array(meta_i['extrinsic_cam2world']).reshape(3,4), mode, transpose_R)
    W2C_j = np.linalg.inv(build_c2w(np.array(meta_j['extrinsic_cam2world']).reshape(3,4), mode, transpose_R))
    # 变到 j 帧相机系
    Xc_j = (W2C_j @ (C2W_i @ Pcam_h).T).T[:,:3]
    z = Xc_j[:,2].clip(1e-6,None)
    # 用 j 的内参投到像素
    fov_v_j = meta_j.get('fov_v_degrees', None); fov_h_j = meta_j.get('fov_h_degrees', None) if use_hfov else None
    fxj,fyj,cxj,cyj = K_from_fov(W,H,fov_v=fov_v_j, fov_h=fov_h_j, halfpx=True)
    u = fxj * (Xc_j[:,0]/z) + cxj
    v = fyj * (Xc_j[:,1]/z) + cyj * (-1.+1.) + cyj  # 已在 rays 里做了 y 取负，这里不再翻转
    # 与 j 帧同一批像素的最近邻匹配：直接用中心对比（粗略 RMS 即可）
    u = u.clip(0,W-1); v=v.clip(0,H-1)
    # 取同一批像素（这里简化比较：投回的点应靠近同物体的轮廓；我们用像素范围当 proxy）
    rms = np.sqrt(((u - W/2.)**2 + (v - H/2.)**2).mean())
    return float(rms)

# 用法示意（替换为你两帧路径）
Di,Mi = load(r'"F:\SteamLibrary\steamapps\common\Grand Theft Auto V\cv_saved\GTAV_2025-08-30_2057837496_depth.npy'); Dj,Mj = load(r'...\GTAV_0002_depth.npy')
for mode in ['c2w','w2c']:
  for tr in [False, True]:
    for use_h in [False, True]:
      print(mode, 'T' if tr else 'N', 'hFOV' if use_h else 'vFOV',
            reproject_rms(Di,Mi,Dj,Mj, mode, tr, use_h))
