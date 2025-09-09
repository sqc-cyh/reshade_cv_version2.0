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

# ... (get_center_depth_float 函数保持不变) ...

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

    d_reversed = get_center_depth_float(os.path.join(data_dir, filename))
    
    if d_reversed is not None and d_reversed > 0.0:
        # --- 关键修改：转换反转深度 ---
        # 如果是反转深度 (近处为1, 远处为0), 我们将其转换为传统深度 (近处为0, 远处为1)
        d_traditional = 1.0 - d_reversed
        
        print(f"z_values.append({real_distance}) # 真实距离")
        print(f"d_values.append({d_traditional:.8f}) # 转换后的深度 'd'")
        print("---")