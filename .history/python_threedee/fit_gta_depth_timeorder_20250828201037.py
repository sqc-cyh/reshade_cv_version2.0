#!/usr/bin/env python3
# -*- coding: utf-8 -*-

import os, re, csv, json, argparse
import numpy as np
from scipy.optimize import curve_fit

# ---------- 解析与预处理 ----------

PATTERN = re.compile(r'^GTAV_\d{4}-\d{2}-\d{2}_(\d+)_depth\.npy$', re.IGNORECASE)

def list_npy_sorted_by_timestamp(dir_path: str) -> list[str]:
    files = []
    for name in os.listdir(dir_path):
        if not name.lower().endswith('.npy'):
            continue
        m = PATTERN.match(name)
        if not m:
            continue
        ts = int(m.group(1))
        files.append((ts, os.path.join(dir_path, name)))
    files.sort(key=lambda t: t[0])            # 小时间戳在前
    return [p for _, p in files]

def read_distances_from_csv(csv_path: str) -> list[float]:
    """
    按行读取 CSV，默认取每行中**第一个可解析的浮点数**为距离（米）。
    这样不依赖列名；若你有固定列，可改成 row['z_m']。
    """
    out = []
    with open(csv_path, newline='', encoding='utf-8') as f:
        reader = csv.reader(f)
        for row in reader:
            if not row:
                continue
            val = None
            for cell in row:
                try:
                    val = float(cell)
                    break
                except Exception:
                    continue
            if val is not None:
                out.append(val)
    if len(out) == 0:
        raise RuntimeError("CSV 中未读到任何数值距离")
    return out

def center_depth(arr: np.ndarray, k: int = 1) -> float:
    a = np.asarray(arr)
    if a.ndim == 3:
        a = a[..., 0]
    h, w = a.shape[-2], a.shape[-1]
    cy, cx = h // 2, w // 2
    if k <= 1:
        return float(a[cy, cx])
    r = k // 2
    patch = a[max(0, cy-r):min(h, cy+r+1), max(0, cx-r):min(w, cx+r+1)]
    return float(np.mean(patch))

# ---------- 拟合 ----------

def model(d, a, b, c, scale):
    return scale / (a + np.exp(b * d + c))

def fit_params(depths: np.ndarray, dists: np.ndarray, scale=1.28, try_invert='auto'):
    def _fit(x):
        # 初值与边界：可按你的数据微调
        p0 = [1e-4, 3.5e2, -8.4e1]               # a, b, c
        bounds = ([1e-12, 1e1,  -2.0e2],
                  [1e-1,  1.0e4,  2.0e2])
        popt, pcov = curve_fit(lambda d_, a, b, c: model(d_, a, b, c, scale),
                               x, dists, p0=p0, bounds=bounds, maxfev=200000)
        pred = model(x, *popt, scale)
        resid = dists - pred
        rmse = float(np.sqrt(np.mean(resid**2)))
        ss_res = float(np.sum(resid**2))
        ss_tot = float(np.sum((dists - np.mean(dists))**2))
        r2 = 1.0 - ss_res/ss_tot if ss_tot > 0 else np.nan
        perr = np.sqrt(np.diag(pcov)) if pcov is not None else np.full_like(popt, np.nan)
        return popt, perr, rmse, r2

    cands = []
    if try_invert in ('no', 'auto'):
        cands.append(('d', *_fit(depths)))
    if try_invert in ('yes', 'auto'):
        cands.append(('1-d', *_fit(1.0 - depths)))

    variant, popt, perr, rmse, r2 = min(cands, key=lambda t: t[3])
    a, b, c = map(float, popt)
    da, db, dc = map(float, perr)
    return (
        {"variant": variant, "scale": float(scale), "a": a, "b": b, "c": c,
         "a_std": float(da), "b_std": float(db), "c_std": float(dc)},
        {"rmse": rmse, "r2": r2}
    )

# ---------- 主流程 ----------

def main():
    ap = argparse.ArgumentParser(
        description="按时间戳对齐 GTA V 深度(.npy) 与 CSV 真实距离并拟合 z = scale / (a + exp(b*d + c))"
    )
    ap.add_argument("--dir", required=True, help=r".npy 目录，例如 F:\SteamLibrary\steamapps\common\Grand Theft Auto V\cv_saved")
    ap.add_argument("--csv", required=True, help="真实距离 CSV（按时间顺序自上而下）")
    ap.add_argument("--scale", type=float, default=1.28, help="公式中的常数 scale，默认 1.28")
    ap.add_argument("--kernel", type=int, default=1, help="中心 k×k 均值（奇数，默认1仅取中心像素）")
    ap.add_argument("--try-invert", choices=["no","yes","auto"], default="auto",
                    help="是否尝试 1-d 并自动选择更优（默认 auto）")
    ap.add_argument("--out", default="gta5_depth_fit.json", help="保存结果 JSON 路径")
    args = ap.parse_args()

    if args.kernel % 2 == 0:
        raise SystemExit("--kernel 必须为奇数")

    npy_files = list_npy_sorted_by_timestamp(args.dir)
    if not npy_files:
        raise SystemExit("未找到匹配命名模式的 .npy 文件（GTAV_YYYY-MM-DD_{timestamp}_depth.npy）")

    z_list = read_distances_from_csv(args.csv)

    # 对齐：按时间排序后的前 N 个 .npy 与 CSV 前 N 行一一对应
    N = min(len(npy_files), len(z_list))
    if N < 3:
        raise SystemExit("有效样本不足（<3）")
    if len(npy_files) != len(z_list):
        print(f"[WARN] .npy 数量({len(npy_files)}) 与 CSV 行数({len(z_list)}) 不一致，按 N={N} 对齐。")

    depths, dists, used_files = [], [], []
    for i in range(N):
        arr = np.load(npy_files[i])
        d = center_depth(arr, k=args.kernel)
        depths.append(d)
        dists.append(float(z_list[i]))
        used_files.append(os.path.basename(npy_files[i]))

    depths = np.asarray(depths, dtype=np.float64)
    dists  = np.asarray(dists,  dtype=np.float64)

    fit_res, diag = fit_params(depths, dists, scale=args.scale, try_invert=args.try_invert)

    print("—— 拟合结果 ——")
    print(f"variant : {fit_res['variant']}  （使用 {'d' if fit_res['variant']=='d' else '1-d'}）")
    print(f"scale   : {fit_res['scale']:.8g}")
    print(f"a       : {fit_res['a']:.12g} ± {fit_res['a_std']:.3g}")
    print(f"b       : {fit_res['b']:.12g} ± {fit_res['b_std']:.3g}")
    print(f"c       : {fit_res['c']:.12g} ± {fit_res['c_std']:.3g}")
    print(f"RMSE(m) : {diag['rmse']:.6g}")
    print(f"R^2     : {diag['r2']:.6g}")

    cpp_line = (f"return static_cast<float>({fit_res['scale']:.9g} / "
                f"({fit_res['a']:.12g} + exp_fast_approx({fit_res['b']:.12g} * "
                f"{('normalizeddepth' if fit_res['variant']=='d' else '(1.0f - normalizeddepth)')} "
                f"+ {fit_res['c']:.12g})) );")
    print("\n// 可直接粘贴到 C++ 的返回表达式：")
    print(cpp_line)

    out = {
        "files": used_files,
        "depth_center": depths.tolist(),
        "distance_m": dists.tolist(),
        "fit": fit_res,
        "diagnostics": diag,
        "equation": "z = scale / (a + exp(b*d + c))",
        "pairing": "files sorted by {timestamp} asc, matched to CSV top-down"
    }
    with open(args.out, "w", encoding="utf-8") as f:
        json.dump(out, f, ensure_ascii=False, indent=2)
    print(f"\n已保存: {args.out}")

if __name__ == "__main__":
    main()
