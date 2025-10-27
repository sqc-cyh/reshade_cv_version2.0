import numpy as np
import matplotlib.pyplot as plt

# 设置 numpy 打印选项，以显示完整的数组
# 如果数组非常大，这可能会产生大量输出
np.set_printoptions(threshold=np.inf)

# 你的 .npy 文件路径
file_path = r"C:\Program Files (x86)\Steam\steamapps\common\Euro Truck Simulator 2\bin\win_x64\cv_saved\EuroTruckSimulator2_2025-10-27_122773412_depth.npy"
try:
    # 加载 .npy 文件
    data = np.load(file_path)
    
    # 打印数组的形状（维度）
    print("数组形状:", data.shape)
    
    # 打印数组的数据类型
    print("数据类型:", data.dtype)
    
    # 创建输出文件路径
    output_file = file_path.replace('.npy', '_output.txt')
    
    # 打开文件用于写入
    with open(output_file, 'w') as f:
        # 写入数组形状和数据类型
        f.write(f"数组形状: {data.shape}\n")
        f.write(f"数据类型: {data.dtype}\n\n")
        
        # 写入完整的数组内容，保留2位小数
        f.write("完整的数组内容 (保留2位小数):\n")
        np.savetxt(f, data, fmt='%.2f', delimiter='\t')
    
    print(f"完整的数组内容已保存到: {output_file}")
    
    # 在控制台仍然显示左上角和中心区域的小部分数据
    print("数组内容 (左上角 10x10，保留2位小数):")
    print(np.array2string(data[:10, :10], formatter={'float_kind': lambda x: "%.2f" % x}))
    
    # 打印数组中心区域 10x10 的部分
    center_y, center_x = data.shape[0] // 2, data.shape[1] // 2
    print("\n数组内容 (中心 10x10，保留2位小数):")
    center_slice = data[center_y-5:center_y+5, center_x-5:center_x+5]
    print(np.array2string(center_slice, formatter={'float_kind': lambda x: "%.2f" % x}))

    # --- 新增：绘制直方图 ---
    print("\n正在生成直方图...")
    
    # 1. 将2D深度图展平为1D数组
    flattened_data = data.flatten()
    
    # 2. 创建一个图形窗口
    plt.figure(figsize=(12, 7))
    
    # 3. 绘制直方图
    plt.hist(flattened_data, bins=100, log=True)
    
    # 4. 添加标题和标签
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