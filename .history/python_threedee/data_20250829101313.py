import numpy as np
import pandas as pd
from scipy.optimize import curve_fit
import matplotlib.pyplot as plt
import struct
import os
import glob

# --- 1. 配置您的文件路径 ---
NPY_FILES_DIR = r'F:\SteamLibrary\steamapps\common\Grand Theft Auto V\cv_saved'
CSV_FILE_PATH = r'F:\SteamLibrary\steamapps\common\Grand Theft Auto V\scripts\gta5_depth_data.csv'


def get_center_float_depth(npy_file_path):
    """
    加载包含原始vi整数的.npy文件，并提取中心区域的浮点深度值。
    """
    try:
        # 加载包含原始 uint32 'vi' 值的数据
        depth_vi_map = np.load(npy_file_path).astype(np.uint32)
    except FileNotFoundError:
        print(f"错误: 找不到文件 {npy_file_path}")
        return None

    h, w = depth_vi_map.shape
    center_y, center_x = h // 2, w // 2

    # 取中心 10x10 区域的中位数以提高稳定性
    patch_size = 5
    half_patch = patch_size // 2
    center_patch = depth_vi_map[center_y - half_patch : center_y + half_patch,
                                center_x - half_patch : center_x + half_patch]
    
    median_vi_value = np.median(center_patch)

    # 将整数 vi 值重新解释为浮点数
    def vi_to_float(vi):
        packed = struct.pack('<I', int(vi))
        return struct.unpack('<f', packed)[0]

    return vi_to_float(median_vi_value)

def fit_depth_data():
    """
    主函数：加载数据，配对，执行拟合，并输出结果。
    """
    # --- 2. 加载真实距离数据 ---
    try:
        df = pd.read_csv(CSV_FILE_PATH)
        real_distances = df['RealDistance_z'].values
        print(f"从 CSV 文件中成功加载 {len(real_distances)} 个距离数据。")
    except FileNotFoundError:
        print(f"错误: 找不到 CSV 文件 {CSV_FILE_PATH}")
        return

    # --- 3. 查找并排序 .npy 文件 ---
    search_pattern = os.path.join(NPY_FILES_DIR, 'GTAV_*_depth.npy')
    npy_files = glob.glob(search_pattern)

    if not npy_files:
        print(f"错误: 在目录 {NPY_FILES_DIR} 中找不到任何匹配的 .npy 文件。")
        return

    # 根据文件名中的时间戳进行排序
    def get_timestamp_from_filename(f):
        # 文件名格式: GTAV_YYYY-MM-DD_TIMESTAMP_depth.npy
        try:
            return int(os.path.basename(f).split('_')[2])
        except (IndexError, ValueError):
            return 0
            
    npy_files.sort(key=get_timestamp_from_filename)
    print(f"找到并排序了 {len(npy_files)} 个 .npy 文件。")

    # --- 4. 配对数据 ---
    depth_data = []
    distance_data = []
    
    num_pairs = min(len(real_distances), len(npy_files))
    if num_pairs == 0:
        print("没有可用于配对的数据。")
        return

    print("\n正在处理和配对数据点...")
    for i in range(num_pairs):
        npy_file = npy_files[i]
        distance = real_distances[i]
        
        float_depth = get_center_float_depth(npy_file)
        if float_depth is not None:
            depth_data.append(float_depth)
            distance_data.append(distance)
            print(f"  - 距离: {distance:.2f}m <-> 浮点深度: {float_depth:.6f}")

    if len(depth_data) < 3:
        print("\n错误: 需要至少3个有效的数据点才能进行曲线拟合。")
        return

    # --- 5. 定义模型并执行曲线拟合 ---
    # 模型: distance = A / (depth + B) - C
    def exponential_func(depth, A, B, C, D):
        return A / (B + np.exp(C * depth + D))

    x_data = np.array(depth_data)
    y_data = np.array(distance_data)

    try:
        # 为新模型提供初始猜测值 (A, B, C, D)
        # 这可能需要根据数据调整，但这是一个不错的起点
        initial_guess = [1.0, 0.1, -100.0, 1.0]
        popt, pcov = curve_fit(exponential_func, x_data, y_data, p0=initial_guess, maxfev=10000)
        A, B, C, D = popt
    except RuntimeError as e:
        print(f"\n曲线拟合失败: {e}")
        print("请尝试调整 initial_guess 或采集更多样化的数据点。")
        return

    # --- 6. 输出结果 ---
    print("\n" + "="*40)
    print("曲线拟合成功！(使用指数模型)")
    print(f"拟合出的参数 [A, B, C, D]: {popt}")
    print("="*40)

    print("\n您可以将以下代码集成到您的 GTAV.cpp 文件中：")
    print("别忘了 #include <cmath> 以使用 expf()")
    print("-" * 50)
    print("float GameGTAV::convert_to_physical_distance_depth_u64(uint64_t depthval) const {")
    print("    // 将 u64 (实际上是 u32) 的位模式重新解释为 float")
    print("    uint32_t depth_as_u32 = static_cast<uint32_t>(depthval);")
    print("    float depth;")
    print("    std::memcpy(&depth, &depth_as_u32, sizeof(float));")
    print("\n    // 如果深度值非常小，可能代表天空或非常远，直接返回大值")
    print("    if (depth < 0.0001f) { // 这个阈值可以根据需要调整")
    print("        return 10000.0f;")
    print("    }")
    print("\n    // 根据曲线拟合得出的指数公式")
    print(f"    return {A:.8f}f / ({B:.8f}f + expf({C:.8f}f * depth + {D:.8f}f));")
    print("}")
    print("-" * 50)

    # --- 7. 可视化拟合效果 ---
    plt.figure(figsize=(10, 6))
    plt.scatter(x_data, y_data, label='原始数据点', color='red', zorder=5)
    
    # 为绘图生成平滑的曲线
    # 注意：我们需要对x数据排序以确保曲线正确绘制
    sorted_indices = np.argsort(x_data)
    smooth_x = x_data[sorted_indices]
    
    plt.plot(smooth_x, exponential_func(smooth_x, *popt), 'b-', label='拟合曲线 (指数模型)')
    
    plt.title('GTA V 深度值与物理距离的拟合曲线 (指数模型)')
    plt.xlabel('浮点深度值')
    plt.ylabel('物理距离 (米)')
    plt.legend()
    plt.grid(True)
    plt.show()


if __name__ == '__main__':
    fit_depth_data()