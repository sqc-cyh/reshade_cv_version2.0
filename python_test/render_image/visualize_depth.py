import numpy as np
import matplotlib.pyplot as plt
import glob
import os
import argparse
import json


def visualize_single_depth(depth_file, show_metadata=True):
    """可视化单个深度文件"""
    print(f"加载: {depth_file}")
    
    # 加载深度数据
    depth = np.load(depth_file).astype(np.float32)
    
    # 归一化到 0-1 范围
    depth_min, depth_max = depth.min(), depth.max()
    print(f"深度范围: {depth_min:.6f} - {depth_max:.6f}")
    depth_max = np.percentile(depth, 50)
    depth_min = np.percentile(depth, 1)
    if depth_max > depth_min:
        depth_normalized = (depth - depth_min) / (depth_max - depth_min)
        depth_normalized = np.clip(depth_normalized, 0, 1)
        # 创建可视化
        plt.figure(figsize=(12, 8))
        
        # 如果是3D数组，只取第一个通道
        # if len(depth.shape) == 3:
        depth_to_show = depth_normalized[:,:,0]
        # else:
        #     depth_to_show = depth_normalized
        plt.imshow(depth_to_show, cmap='viridis')
        plt.colorbar(label='Normalized Depth (0=near, 1=far)')
        
        # 添加标题和标签
        base_name = os.path.splitext(os.path.basename(depth_file))[0]
        plt.title(f'Depth Visualization: {base_name}')
        plt.xlabel('Width (pixels)')
        plt.ylabel('Height (pixels)')
        
        # 显示元数据（如果存在）
        if show_metadata:
            metadata_file = depth_file.replace('.npy', '_metadata.txt')
            pose_file = depth_file.replace('.npy', '_pose.json')
            
            metadata_text = ""
            if os.path.exists(metadata_file):
                with open(metadata_file, 'r', encoding='utf-8') as f:
                    lines = f.readlines()[:5]  # 只显示前几行
                    metadata_text += "Metadata:\n" + "".join(lines)
            
            if os.path.exists(pose_file):
                with open(pose_file, 'r', encoding='utf-8') as f:
                    pose_data = json.load(f)
                    loc = pose_data['location']
                    rot = pose_data['rotation']
                    metadata_text += f"\nCamera Pose:\n"
                    metadata_text += f"Location: ({loc['x']:.2f}, {loc['y']:.2f}, {loc['z']:.2f})\n"
                    metadata_text += f"Rotation: ({rot['pitch']:.2f}, {rot['yaw']:.2f}, {rot['roll']:.2f})\n"
                    metadata_text += f"FOV: {pose_data['fov']:.2f}°"
            
            if metadata_text:
                plt.figtext(0.02, 0.02, metadata_text, fontsize=8, family='monospace',
                           bbox=dict(boxstyle="round,pad=0.3", facecolor="lightgray", alpha=0.8))
        
        plt.tight_layout()
        plt.show()
    else:
        print("警告: 深度数据没有变化，无法可视化")


def visualize_all_depths(pattern="output/depth_*.npy", max_files=None):
    """可视化所有匹配的深度文件"""
    depth_files = glob.glob(pattern)
    if not depth_files:
        print(f"没有找到匹配的depth文件: {pattern}")
        return
    
    # 按文件名排序
    depth_files.sort()
    
    if max_files:
        depth_files = depth_files[:max_files]
    
    print(f"找到 {len(depth_files)} 个深度文件")
    
    for i, depth_file in enumerate(depth_files):
        print(f"\n=== 文件 {i+1}/{len(depth_files)} ===")
        visualize_single_depth(depth_file, show_metadata=True)
        
        if i < len(depth_files) - 1:
            input("按回车键继续到下一个文件...")


def find_latest_depth():
    """找到最新的深度文件"""
    # 尝试不同的文件模式
    patterns = [
        "output/depth_*.npy",
        "output/render_depth_*.npy"
    ]
    
    all_files = []
    for pattern in patterns:
        all_files.extend(glob.glob(pattern))
    
    if not all_files:
        print("没有找到depth文件")
        print("请确保已经运行了渲染脚本并生成了深度数据")
        return None
    
    latest_depth = max(all_files, key=os.path.getctime)
    return latest_depth


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description='可视化深度数据')
    parser.add_argument('--file', help='指定要可视化的深度文件')
    parser.add_argument('--pattern', default="output/depth_*.npy", help='匹配深度文件的模式')
    parser.add_argument('--all', action='store_true', help='可视化所有匹配的深度文件')
    parser.add_argument('--latest', action='store_true', help='可视化最新的深度文件')
    parser.add_argument('--max', type=int, help='限制可视化的最大文件数量')
    parser.add_argument('--no-metadata', action='store_true', help='不显示元数据信息')
    
    args = parser.parse_args()
    
    if args.file:
        # 可视化指定文件
        if os.path.exists(args.file):
            visualize_single_depth(args.file, show_metadata=not args.no_metadata)
        else:
            print(f"文件不存在: {args.file}")
    elif args.all:
        # 可视化所有文件
        visualize_all_depths(args.pattern, max_files=args.max)
    elif args.latest:
        # 可视化最新文件
        latest_file = find_latest_depth()
        if latest_file:
            visualize_single_depth(latest_file, show_metadata=not args.no_metadata)
    else:
        # 默认行为：可视化最新文件
        latest_file = find_latest_depth()
        if latest_file:
            visualize_single_depth(latest_file, show_metadata=not args.no_metadata)
