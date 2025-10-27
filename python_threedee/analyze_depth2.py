import numpy as np
import matplotlib.pyplot as plt

# 设置 numpy 打印选项
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
    output_file = file_path.replace('.npy', '_binned_output.txt')
    
    # 打开文件用于写入
    with open(output_file, 'w') as f:
        # 写入数组形状和数据类型
        f.write(f"数组形状: {data.shape}\n")
        f.write(f"数据类型: {data.dtype}\n\n")
        
        # --- 桶排序/分箱统计 ---
        f.write("=== 深度值分箱统计 ===\n")
        
        # 方法1: 自动分箱（基于数据范围）
        min_val = np.min(data)
        max_val = np.max(data)
        f.write(f"数据范围: [{min_val:.6f}, {max_val:.6f}]\n")
        
        # 创建分箱（根据数据范围动态调整）
        if max_val - min_val <= 1.0:
            # 如果数据范围较小，使用更细的分箱
            bins = np.linspace(min_val, max_val, 101)  # 100个箱子
        else:
            # 如果数据范围较大，使用对数分箱或固定数量分箱
            bins = np.linspace(min_val, max_val, 2001)  # 200个箱子
        
        # 计算每个箱子的统计信息
        hist, bin_edges = np.histogram(data, bins=bins)
        
        f.write(f"\n分箱数量: {len(bins)-1}\n")
        f.write("分箱统计:\n")
        f.write("箱号\t范围起点\t范围终点\t像素数量\t占比(%)\n")
        f.write("-" * 80 + "\n")
        
        total_pixels = data.size
        for i in range(len(hist)):
            if hist[i] > 0:  # 只输出有数据的箱子
                percentage = (hist[i] / total_pixels) * 100
                f.write(f"{i:4d}\t{bin_edges[i]:.6f}\t{bin_edges[i+1]:.6f}\t{hist[i]:8d}\t{percentage:.4f}%\n")
        
    print(f"分箱统计结果已保存到: {output_file}")
    
    # 在控制台显示摘要信息
    print(f"\n数据摘要:")
    print(f"范围: [{min_val:.6f}, {max_val:.6f}]")
    print(f"均值: {np.mean(data):.6f}")
    print(f"中位数: {np.median(data):.6f}")
    print(f"标准差: {np.std(data):.6f}")
    
    # --- 绘制直方图 ---
    print("\n正在生成直方图...")
    
    flattened_data = data.flatten()
    plt.figure(figsize=(12, 7))
    plt.hist(flattened_data, bins=100, log=True)
    
    filename = file_path.split('V\\')[-1]
    plt.title(f"深度值分布直方图\n(文件: {filename})")
    plt.xlabel("深度值")
    plt.ylabel("像素数量 (对数刻度)")
    plt.grid(True, which="both", linestyle='--', linewidth=0.5)
    plt.show()

except FileNotFoundError:
    print(f"错误: 文件未找到 '{file_path}'")
except Exception as e:
    print(f"读取文件时发生错误: {e}")