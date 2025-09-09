import numpy as np

# 设置 numpy 打印选项，以显示完整的数组
# 如果数组非常大，这可能会产生大量输出
np.set_printoptions(threshold=np.inf)

# 你的 .npy 文件路径
file_path = r"F:\SteamLibrary\steamapps\common\Grand Theft Auto V\cv_saved\GTAV_2025-08-28_94467964_depth.npy"
try:
    # 加载 .npy 文件
    data = np.load(file_path)
    
    # 打印数组的形状（维度）
    print("数组形状:", data.shape)
    
    # 打印数组的数据类型
    print("数据类型:", data.dtype)
    
    # 打印数组内容
    # 打印数组内容 (仅显示左上角 10x10 部分)
    print("数组内容 (左上角 10x10):")
    print(data[:10, :10])

    # 打印数组中心区域 10x10 的部分
    # 图像尺寸为 (1052, 1914)，中心点大约在 (526, 957)
    print("\n数组内容 (中心 10x10):")
    center_slice = data[520:530, 950:960]
    print(center_slice)

except FileNotFoundError:
    print(f"错误: 文件未找到 '{file_path}'")
except Exception as e:
    print(f"读取文件时发生错误: {e}")