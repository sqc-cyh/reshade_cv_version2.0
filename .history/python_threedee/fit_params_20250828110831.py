import numpy as np
from scipy.optimize import curve_fit

# 这是您 C++ 公式的 Python 版本
def inverse_perspective_z(d, n, f):
    num = 2.0 * n * f
    den = (f + n) - (2.0 * d - 1.0) * (f - n)
    return num / np.maximum(1e-12, den)

# --- 在这里填入您从第 1 步收集到的所有数据 ---
# 示例数据，请务必替换为您自己的测量值！
z_values = np.array([20.8, 55.2, 101.5, 250.9, 510.1, 850.5]) # 游戏内真实距离 (米)
d_values = np.array([0.005, 0.013, 0.024, 0.058, 0.115, 0.189]) # 对应的归一化深度 'd'

# 初始猜测值 (可以从 C++ 代码中的值开始)
initial_guess = [0.01, 1000.0]

# 使用 curve_fit 进行拟合
try:
    popt, pcov = curve_fit(inverse_perspective_z, d_values, z_values, p0=initial_guess, maxfev=5000)
    
    n_fit, f_fit = popt
    
    print("拟合成功！")
    print("========================================")
    print(f"最佳近裁剪面 (NEAR_CLIP): {n_fit:.8f}")
    print(f"最佳远裁剪面 (FAR_CLIP):  {f_fit:.8f}")
    print("========================================")
    print("请将以上两个值更新回您的 GTAV.cpp 文件中。")

except RuntimeError:
    print("拟合失败。请检查您的数据点是否准确，或者尝试更多的/分布更广的数据点。")
