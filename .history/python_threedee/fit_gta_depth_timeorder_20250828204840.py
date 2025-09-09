import os, re, csv, json, argparse
import numpy as np
from scipy.optimize import curve_fit

PAT = re.compile(r'^GTAV_\d{4}-\d{2}-\d{2}_(\d+)_depth\.npy$', re.IGNORECASE)

def list_npy_sorted(d):
    items=[]
    for n in os.listdir(d):
        m = PAT.match(n)
        if m: items.append((int(m.group(1)), os.path.join(d,n)))
    items.sort(key=lambda t:t[0])
    return [p for _,p in items]

def read_csv(csv_path):
    zs=[]
    with open(csv_path, newline='', encoding='utf-8') as f:
        for row in csv.reader(f):
            for cell in row:
                try: zs.append(float(cell)); break
                except: pass
    if not zs: raise SystemExit("CSV 中未读到任何距离")
    return zs

def center_depth(a, k=3):
    a=np.asarray(a)
    if a.ndim==3: a=a[...,0]
    h,w=a.shape[-2],a.shape[-1]
    cy,cx=h//2,w//2; r=k//2
    return float(np.mean(a[cy-r:cy+r+1, cx-r:cx+r+1]))

def z_from_d(d, P22, P32):     # z = P32 / (d - P22)
    return P32 / np.maximum(d - P22, 1e-9)

def fit_P22_P32(d, z):
    # 两个候选：d 与 1-d
    def _fit(x):
        p0=[0.1, np.median(z)]                   # P22 初值, P32 初值
        bounds=([-1.0, 1e-6], [ 1.0, 1e6])       # 适度宽松
        popt, pcov = curve_fit(z_from_d, x, z, p0=p0, bounds=bounds, maxfev=200000)
        pred = z_from_d(x, *popt)
        resid=z-pred
        rmse=float(np.sqrt(np.mean(resid**2)))
        ss_res=float(np.sum(resid**2)); ss_tot=float(np.sum((z-np.mean(z))**2))
        r2=1.0-ss_res/ss_tot if ss_tot>0 else np.nan
        perr=np.sqrt(np.diag(pcov)) if pcov is not None else np.full_like(popt, np.nan)
        return popt, perr, rmse, r2
    cands=[]
    popt, perr, rmse, r2 = _fit(d);      cands.append(('d',   popt, perr, rmse, r2))
    poptI, perrI, rmseI, r2I = _fit(1-d); cands.append(('1-d', poptI, perrI, rmseI, r2I))
    return min(cands, key=lambda t:t[3])  # 选 RMSE 最小

def main():
    ap=argparse.ArgumentParser()
    ap.add_argument('--dir', required=True)
    ap.add_argument('--csv', required=True)
    ap.add_argument('--kernel', type=int, default=3)
    ap.add_argument('--out', default='gta5_p22p32_fit.json')
    args=ap.parse_args()
    if args.kernel%2==0: raise SystemExit("--kernel 必须为奇数")

    files=list_npy_sorted(args.dir)
    zs=read_csv(args.csv)
    N=min(len(files), len(zs))
    if N<3: raise SystemExit("有效样本不足 (<3)")

    ds=[]; z_used=[]; names=[]
    for i in range(N):
        arr=np.load(files[i])
        ds.append(center_depth(arr, k=args.kernel))
        z_used.append(float(zs[i]))
        names.append(os.path.basename(files[i]))
    d=np.asarray(ds, dtype=np.float64); z=np.asarray(z_used, dtype=np.float64)

    variant, popt, perr, rmse, r2 = fit_P22_P32(d, z)
    P22, P32 = map(float, popt); sP22,sP32 = map(float, perr)

    print("—— 拟合结果 ——")
    print(f"variant : {variant}  （使用 {'d' if variant=='d' else '1-d'}）")
    print(f"P22     : {P22:.12g} ± {sP22:.3g}")
    print(f"P32     : {P32:.12g} ± {sP32:.3g}")
    print(f"RMSE(m) : {rmse:.6g}")
    print(f"R^2     : {r2:.6g}")

    # 生成可替换的 C++ 片段
    nd = "normalizeddepth" if variant=='d' else "(1.0f - normalizeddepth)"
    cpp = f"return static_cast<float>({P32:.9g} / max({nd} - {P22:.12g}f, 1e-6f));"
    print("\n// C++：")
    print(cpp)

    out={
        "files":names,
        "depth_center":ds,
        "distance_m":z_used,
        "variant":variant,
        "P22":P22,"P32":P32,"P22_std":sP22,"P32_std":sP32,
        "rmse":rmse,"r2":r2,
        "equation":"z = P32 / (d' - P22)"
    }
    with open(args.out,'w',encoding='utf-8') as f:
        json.dump(out,f,ensure_ascii=False,indent=2)

if __name__=="__main__":
    main()
