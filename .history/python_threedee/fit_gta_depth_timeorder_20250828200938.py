#!/usr/bin/env python3
# -*- coding: utf-8 -*-

import os, re, csv, json, argparse
import numpy as np
from scipy.optimize import curve_fit

PAT = re.compile(r'^GTAV_\d{4}-\d{2}-\d{2}_(\d+)_depth\.npy$', re.IGNORECASE)

def list_npy_sorted(dir_path: str):
    items = []
    for n in os.listdir(dir_path):
        if not n.lower().endswith('.npy'): continue
        m = PAT.match(n)
        if not m: continue
        items.append((int(m.group(1)), os.path.join(dir_path, n)))
    items.sort(key=lambda t: t[0])  # 小的在前
    return [p for _, p in items]

def read_distances(csv_path: str):
    zs = []
    with open(csv_path, newline='', encoding='utf-8') as f:
        for row in csv.reader(f):
            for cell in row:
                try: zs.append(float(cell)); break
                except: pass
    if not zs: raise RuntimeError("CSV 中未读到任何距离数值")
    return zs

def center_depth(a: np.ndarray, k: int = 1) -> float:
    a = np.asarray(a)
    if a.ndim == 3: a = a[..., 0]
    h, w = a.shape[-2], a.shape[-1]
    cy, cx = h//2, w//2
    if k <= 1: return float(a[cy, cx])
    r = k//2
    return float(np.mean(a[cy-r:cy+r+1, cx-r:cx+r+1]))

def model(d, a, b, c, s):  # z = s / (a + exp(b*d + c))
    return s / (a + np.exp(b*d + c))

def fit_params(d, z, try_invert='auto'):
    d = np.asarray(d, dtype=np.float64)
    z = np.asarray(z, dtype=np.float64)

    # 初值与边界（可按数据再调）
    # a>0 防止分母退化；b>0 常见；c 无符；s>0
    p0     = [1e-4, 3.5e2, -8.4e1, max(1.0, np.median(z))]
    bounds = ([1e-12, 1e1,  -2.0e2, 1e-6],
              [1e-1,   1e4,  2.0e2,  1e6])

    def _fit(x):
        popt, pcov = curve_fit(model, x, z, p0=p0, bounds=bounds, maxfev=300000)
        pred = model(x, *popt)
        resid = z - pred
        rmse = float(np.sqrt(np.mean(resid**2)))
        ss_res = float(np.sum(resid**2))
        ss_tot = float(np.sum((z - np.mean(z))**2))
        r2 = 1.0 - ss_res/ss_tot if ss_tot > 0 else np.nan
        perr = np.sqrt(np.diag(pcov)) if pcov is not None else np.full_like(popt, np.nan)
        return popt, perr, rmse, r2

    cands = []
    if try_invert in ('no','auto'):  cands.append(('d',   *_fit(d)))
    if try_invert in ('yes','auto'): cands.append(('1-d', *_fit(1.0 - d)))

    variant, popt, perr, rmse, r2 = min(cands, key=lambda t: t[3])
    a,b,c,s = map(float, popt)
    da,db,dc,ds = map(float, perr)
    return (
        {"variant": variant, "a": a, "b": b, "c": c, "scale": s,
         "a_std": da, "b_std": db, "c_std": dc, "scale_std": ds},
        {"rmse": rmse, "r2": r2}
    )

def main():
    ap = argparse.ArgumentParser(description="按时间顺序匹配 .npy 与 CSV 并拟合 z = scale / (a + exp(b*d + c))（scale 同时拟合）")
    ap.add_argument("--dir", required=True, help=r".npy 目录（如 F:\SteamLibrary\steamapps\common\Grand Theft Auto V\cv_saved）")
    ap.add_argument("--csv", required=True, help="真实距离 CSV（顶端为最早拍到）")
    ap.add_argument("--kernel", type=int, default=1, help="中心 k×k 均值，奇数（默认1）")
    ap.add_argument("--try-invert", choices=["no","yes","auto"], default="auto", help="尝试用 1-d 并择优（默认 auto）")
    ap.add_argument("--out", default="gta5_depth_fit.json", help="输出 JSON")
    args = ap.parse_args()
    if args.kernel % 2 == 0: raise SystemExit("--kernel 必须为奇数")

    npy_files = list_npy_sorted(args.dir)
    zs = read_distances(args.csv)

    N = min(len(npy_files), len(zs))
    if N < 3: raise SystemExit("有效样本不足（<3）")
    if len(npy_files) != len(zs):
        print(f"[WARN] .npy 数({len(npy_files)}) 与 CSV 行数({len(zs)}) 不一致，按 N={N} 对齐。")

    ds, z_used, names = [], [], []
    for i in range(N):
        d = center_depth(np.load(npy_files[i]), k=args.kernel)
        ds.append(d); z_used.append(float(zs[i])); names.append(os.path.basename(npy_files[i]))

    fit_res, diag = fit_params(ds, z_used, try_invert=args.try_invert)

    print("—— 拟合结果 ——")
    print(f"variant : {fit_res['variant']} （使用 {'d' if fit_res['variant']=='d' else '1-d'}）")
    print(f"a       : {fit_res['a']:.12g} ± {fit_res['a_std']:.3g}")
    print(f"b       : {fit_res['b']:.12g} ± {fit_res['b_std']:.3g}")
    print(f"c       : {fit_res['c']:.12g} ± {fit_res['c_std']:.3g}")
    print(f"scale   : {fit_res['scale']:.12g} ± {fit_res['scale_std']:.3g}")
    print(f"RMSE(m) : {diag['rmse']:.6g}")
    print(f"R^2     : {diag['r2']:.6g}")

    cpp = (f"return static_cast<float>({fit_res['scale']:.9g} / "
           f"({fit_res['a']:.12g} + exp_fast_approx({fit_res['b']:.12g} * "
           f"{('normalizeddepth' if fit_res['variant']=='d' else '(1.0f - normalizeddepth)')} "
           f"+ {fit_res['c']:.12g})) );")
    print("\n// C++ 公式：")
    print(cpp)

    out = {
        "files": names,
        "depth_center": ds,
        "distance_m": z_used,
        "fit": fit_res,
        "diagnostics": diag,
        "equation": "z = scale / (a + exp(b*d + c))",
        "pairing": "files sorted by {timestamp} asc ↔ CSV top-down"
    }
    with open(args.out, "w", encoding="utf-8") as f:
        json.dump(out, f, ensure_ascii=False, indent=2)
    print(f"\n已保存: {args.out}")

if __name__ == "__main__":
    main()
