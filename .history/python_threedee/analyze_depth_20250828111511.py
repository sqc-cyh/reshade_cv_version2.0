import numpy as np
import os

def get_center_depth_float(npy_file_path):
    """读取.npy深度文件，直接返回中心点的 float32 值"""
    if not os.path.exists(npy_file_path):
        return None
    
    depth_data = np.load(npy_file_path)
    
    # 确认数据类型是 float32
    if depth_data.dtype != np.float32:
        print(f"警告: 文件 {npy_file_path} 的数据类型不是 float32, 而是 {depth_data.dtype}")
        return None

    height, width = depth_data.shape
    center_y, center_x = height // 2, width // 2
    
    # 直接返回中心点的 float32 值
    return depth_data[center_y, center_x]

# --- 使用示例 ---
data_dir = r'F:\SteamLibrary\steamapps\common\Grand Theft Auto V\cv_saved' # <--- 您的目录
saved_files = [f for f in os.listdir(data_dir) if f.endswith('.npy')]

print("请将以下数据填入到下一步的拟合脚本中：")
print("==========================================")
for filename in saved_files:
    try:
        distance_str = filename.replace('.npy', '')
        real_distance = float(distance_str)
    except ValueError:
        continue

    d_value = get_center_depth_float(os.path.join(data_dir, filename))
    
    if d_value is not None and d_value > 0.0: # 忽略值为 0 的数据点
        print(f"z_values.append({real_distance}) # 真实距离")
        print(f"d_values.append({d_value:.8f}) # 归一化深度 'd'")
        print("---")