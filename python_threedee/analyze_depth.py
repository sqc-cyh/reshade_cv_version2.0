import numpy as np
import matplotlib.pyplot as plt

# 设置 numpy 打印选项，以显示完整的数组
# 如果数组非常大，这可能会产生大量输出
np.set_printoptions(threshold=np.inf)

# 你的 .npy 文件路径
file_path = r"C:\Program Files (x86)\Steam\steamapps\common\Assassins Creed Odyssey\cv_saved\AssassinsCreedOdyssey_2025-09-24_1089608613_depth.npy"
try:
    # 加载 .npy 文件
    data = np.load(file_path)
    # data_uint32 = data.astype(np.uint32)

    # # 将 uint32 的位模式重新解释为 float32
    # # 这等同于 C++ 中的 memcpy 操作
    # data = data_uint32.view(np.float32)
    # 打印数组的形状（维度）
    print("数组形状:", data.shape)
    
    # 打印数组的数据类型
    print("数据类型:", data.dtype)
    
    # 打印数组内容 (仅显示左上角 10x10 部分)
    print("数组内容 (左上角 10x10):")
    print(data[:10, :10])

    # 打印数组中心区域 10x10 的部分
    # 图像尺寸为 (1052, 1914)，中心点大约在 (526, 957)
    # 注意：如果您的分辨率不同，需要调整这些索引
    center_y, center_x = data.shape[0] // 2, data.shape[1] // 2
    print("\n数组内容 (中心 10x10):")
    center_slice = data[center_y-5:center_y+5, center_x-5:center_x+5]
    print(center_slice)

    # --- 新增：绘制直方图 ---
    print("\n正在生成直方图...")
    
    # 1. 将2D深度图展平为1D数组
    flattened_data = data.flatten()
    
    # 2. 创建一个图形窗口
    plt.figure(figsize=(12, 7))
    
   # 3. 绘制直方图
    # bins参数控制柱子的数量，可以调整以获得更精细或更粗略的视图
    # log=True 使用对数刻度，这对于深度图通常很有用，因为某些值的像素数可能远超其他值
    plt.hist(flattened_data, bins=100, log=True)
    
    # 4. 添加标题和标签
    # 先将文件名提取出来，避免在 f-string 中使用反斜杠
    filename = file_path.split('V\\')[-1]
    plt.title(f"深度值分布直方图\n(文件: {filename})")
    plt.xlabel("深度值")
    plt.ylabel("像素数量 (对数刻度)")
    plt.grid(True, which="both", linestyle='--', linewidth=0.5)
    
    
    # 5. 显示图形
    plt.show()


except FileNotFoundError:
    print(f"错误: 文件未找到 '{file_path}'")
except Exception as e:
    print(f"读取文件时发生错误: {e}")