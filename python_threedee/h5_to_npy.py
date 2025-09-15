import h5py
import numpy as np
import os
import matplotlib.pyplot as plt

def inspect_h5_file(h5_path):
    """检查单个 HDF5 文件的内容"""
    print(f"\n🔍 检查文件: {os.path.basename(h5_path)}")
    
    with h5py.File(h5_path, 'r') as f:
        # 1. 查看 dataset 形状和类型
        dataset = f['depth']
        data = dataset[:]
        print(f"  Shape: {data.shape}")          # (T, H, W)
        print(f"  Dtype: {data.dtype}")
        print(f"  数据范围: [{data.min():.3f}, {data.max():.3f}]")

        # 2. 查看 attributes（元数据）
        attrs = dict(dataset.attrs.items())
        print("  Attributes:")
        for key, value in attrs.items():
            print(f"    {key}: {value}")

        # 3. 保存为 .npy（可选）
        npy_path = os.path.splitext(os.path.basename(h5_path))[0] + '.npy'
        np.save(npy_path, data)
        np_size = os.path.getsize(npy_path)
        print(f"  保存为: {npy_path} ({np_size:,} bytes)")

        # 4. 可视化第一帧 depth
        plt.figure(figsize=(12, 6))
        
        # 显示第一帧
        plt.subplot(1, 2, 1)
        d0 = data[0]  # 第一帧
        # 将深度图归一化到 0~1 显示（远=白，近=黑）
        d0_norm = np.clip((d0 - d0.min()) / (d0.max() - d0.min() + 1e-8), 0, 1)
        plt.imshow(d0_norm, cmap='gray')
        plt.title(f"Depth Frame 0\nRange: {d0.min():.2f} ~ {d0.max():.2f}")
        plt.colorbar()

        # 显示最后一帧
        plt.subplot(1, 2, 2)
        d_last = data[-1]
        d_last_norm = np.clip((d_last - d_last.min()) / (d_last.max() - d_last.min() + 1e-8), 0, 1)
        plt.imshow(d_last_norm, cmap='gray')
        plt.title(f"Depth Last Frame\nRange: {d_last.min():.2f} ~ {d_last.max():.2f}")
        plt.colorbar()

        plt.suptitle(f"{os.path.basename(h5_path)}\nShape: {data.shape}, FPS: {attrs.get('fps', '?')}")
        plt.tight_layout()
        plt.show()

    return data

# === 主程序开始 ===
if __name__ == "__main__":
    # 替换为你自己的路径
    dir_path = r"C:\Program Files (x86)\Steam\steamapps\common\Cyberpunk 2077\bin\x64\cv_saved\actions_2025-09-12_1229057284"
    
    # 获取所有 depth_group_*.h5 文件并排序
    import glob
    h5_files = sorted(glob.glob(os.path.join(dir_path, "depth_group_*.h5")))
    
    if not h5_files:
        print("❌ 未找到 depth_group_*.h5 文件，请检查路径")
    else:
        print(f"✅ 找到 {len(h5_files)} 个 HDF5 文件")

        all_data = []
        for h5_path in h5_files[:2]:  # 只看前两个，太多会弹窗
            data = inspect_h5_file(h5_path)
            all_data.append(data)

        # 可选：合并所有数据（用于分析）
        # full_depth = np.concatenate(all_data, axis=0)
        # print(f"总共加载 {full_depth.shape[0]} 帧 depth")
