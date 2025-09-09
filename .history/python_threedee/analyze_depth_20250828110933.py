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
data_dir = r'F:\SteamLibrary\steamapps\common\Grand Theft Auto V\cv_saved' # <--- 修改为您的数据保存目录
saved_files = [f for f in os.listdir(data_dir) if f.endswith('_depth.npy')]

print("请将以下数据填入到下一步的拟合脚本中：")
print("==========================================")
for filename in saved_files:
    # 从文件名或您的笔记中获取对应的真实距离
    # 这里需要您手动关联，例如文件名中包含距离
    # real_distance = float(filename.split('_')[1]) # 这是一个例子
    
    d_value = get_center_normalized_depth(os.path.join(data_dir, filename))
    
    if d_value is not None:
        # 您需要将这里的 '???' 替换为您记录的真实距离
        print(f"真实距离: ??? m  ->  归一化深度 'd': {d_value:.8f}")
