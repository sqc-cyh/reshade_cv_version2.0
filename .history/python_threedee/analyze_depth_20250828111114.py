import numpy as np
import os

def get_center_normalized_depth(npy_file_path):
    """读取.npy深度文件，返回中心点归一化后的深度值 'd'"""
    if not os.path.exists(npy_file_path):
        print(f"错误: 找不到文件 {npy_file_path}")
        return None
    
    depth_data = np.load(npy_file_path)
    
    # 获取图像中心点坐标
    height, width = depth_data.shape
    center_y, center_x = height // 2, width // 2
    
    # 提取中心点的原始 uint64 值
    raw_depth_value = depth_data[center_y, center_x]
    
    # 必须使用与 C++ 代码完全相同的公式进行归一化
    # const double d = static_cast<double>(depthval) / 4294967295.0;
    normalized_d = float(raw_depth_value) / 4294967295.0
    
    return normalized_d

# --- 使用示例 ---
# 遍历您保存数据的所有文件，并记录下结果
# 假设您的文件保存在 'F:\gta_data'
# ... (上半部分 get_center_normalized_depth 函数保持不变) ...

# --- 使用示例 ---
# 遍历您保存数据的所有文件，并记录下结果
data_dir = r'F:\SteamLibrary\steamapps\common\Grand Theft Auto V\cv_saved' # <--- 确保这是您正确的目录
saved_files = [f for f in os.listdir(data_dir) if f.endswith('.npy')]

print("请将以下数据填入到下一步的拟合脚本中：")
print("==========================================")
for filename in saved_files:
    real_distance = -1.0
    try:
        # 从文件名（例如 '153.npy'）中提取数字部分
        distance_str = filename.replace('.npy', '')
        real_distance = float(distance_str)
    except ValueError:
        print(f"无法从文件名 '{filename}' 中解析距离。跳过此文件。")
        continue # 跳过无法解析的文件

    d_value = get_center_normalized_depth(os.path.join(data_dir, filename))
    
    if d_value is not None:
        # 现在它可以自动打印出配对好的数据
        print(f"z_values.append({real_distance}) # 真实距离")
        print(f"d_values.append({d_value:.8f}) # 归一化深度 'd'")
        print("---")

