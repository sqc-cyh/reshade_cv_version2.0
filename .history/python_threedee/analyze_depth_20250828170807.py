import numpy as np
import os

# --- 关键：定义一个函数来处理单个文件 ---
def get_center_depth(file_path):
    try:
        data = np.load(file_path)
        
        # 获取图像的高度和宽度
        height, width = data.shape
        
        # 计算中心点坐标
        center_y = height // 2
        center_x = width // 2
        
        # 提取中心点的深度值
        center_depth_value = data[center_y, center_x]
        
        return center_depth_value
        
    except Exception as e:
        print(f"处理文件 {file_path} 时出错: {e}")
        return None

# --- 主逻辑：循环处理多个文件 ---

# 定义您保存 .npy 文件的目录
data_directory = r"E:\path\to\your\gta5_captures" # <--- 修改为您的目录

# 定义您的数据对（文件名和对应的真实距离）
# 您可以手动维护这个列表，或者通过解析文件名来自动生成
capture_files = {
    "gta5_5.0m.npy": 5.0,
    "gta5_10.0m.npy": 10.0,
    "gta5_20.0m.npy": 20.0,
    "gta5_40.0m.npy": 40.0,
    "gta5_80.0m.npy": 80.0,
    "gta5_150.0m.npy": 150.0,
    # ... 添加更多文件
}

print("请将以下数据填入到 fit_params.py 中：")
print("==========================================")

z_values_str = "z_values = np.array(["
d_values_str = "d_values = np.array(["

for filename, distance in capture_files.items():
    full_path = os.path.join(data_directory, filename)
    depth_val = get_center_depth(full_path)
    
    if depth_val is not None:
        print(f"z = {distance:.1f} m  ->  d = {depth_val:.8f}")
        z_values_str += f"{distance:.1f}, "
        d_values_str += f"{depth_val:.8f}, "

print("\n# 自动生成的代码片段:")
print(z_values_str.rstrip(", ") + "])")
print(d_values_str.rstrip(", ") + "])")