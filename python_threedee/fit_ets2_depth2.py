import numpy as np
import matplotlib.pyplot as plt
from PIL import Image
import os

def manual_boundary_selector(rgb_path, depth_path, far_value):
    """
    手动选择边界点并输出RGB和深度值
    
    参数:
        rgb_path: RGB图像路径
        depth_path: 深度npy文件路径  
        far_value: 设置的far值（物理距离）
    """
    
    # 加载RGB图像和深度数据
    rgb_img = np.array(Image.open(rgb_path))
    depth_data = np.load(depth_path)
    
    print(f"\n=== 分析 far={far_value} 的情况 ===")
    print(f"RGB图像形状: {rgb_img.shape}")
    print(f"深度数据形状: {depth_data.shape}")
    print(f"深度值范围: [{depth_data.min():.2f}, {depth_data.max():.2f}]")
    
    # 显示图像
    fig, (ax1, ax2) = plt.subplots(1, 2, figsize=(15, 6))
    
    # 显示RGB图像
    ax1.imshow(rgb_img)
    ax1.set_title(f'RGB Image (far={far_value}m)')
    ax1.axis('on')
    
    # 显示深度图像（归一化以便显示）
    depth_display = depth_data - depth_data.min()
    depth_display = depth_display / depth_display.max() if depth_display.max() > 0 else depth_display
    im = ax2.imshow(depth_display, cmap='viridis')
    ax2.set_title('Depth Map')
    ax2.axis('on')
    plt.colorbar(im, ax=ax2, label='Normalized Depth')
    
    plt.tight_layout()
    plt.show()
    
    # 手动选择边界点
    print("\n请在图像中识别渲染边界（黑色区域与彩色区域的交界处）")
    print("我们将从边界区域选择3个点进行分析...")
    
    boundary_points = []
    
    for i in range(3):
        print(f"\n--- 选择第 {i+1} 个边界点 ---")
        
        # 获取用户输入的坐标
        try:
            y = int(input("请输入行坐标 (y): "))
            x = int(input("请输入列坐标 (x): "))
            
            # 检查坐标是否有效
            if 0 <= y < rgb_img.shape[0] and 0 <= x < rgb_img.shape[1]:
                # 获取RGB值
                if len(rgb_img.shape) == 3:
                    rgb_values = rgb_img[y, x, :3]  # 取前三个通道
                else:
                    rgb_values = [rgb_img[y, x]] * 3
                
                # 获取深度值
                depth_value = depth_data[y, x]
                
                boundary_points.append({
                    'coord': (x, y),
                    'rgb': rgb_values,
                    'depth': depth_value
                })
                
                print(f"点 {i+1}: 坐标({x}, {y}), RGB{rgb_values}, 深度值={depth_value:.2f}")
                
            else:
                print("坐标超出范围，请重新输入")
                i -= 1  # 重新选择这个点
                
        except ValueError:
            print("请输入有效的整数坐标")
            i -= 1  # 重新选择这个点
    
    # 输出结果表格
    print(f"\n=== far={far_value}m 的边界点分析结果 ===")
    print("序号 | 坐标(x,y) |     R, G, B     |    深度值    | 物理距离")
    print("-" * 65)
    
    for i, point in enumerate(boundary_points):
        rgb_str = f"{point['rgb'][0]:3d}, {point['rgb'][1]:3d}, {point['rgb'][2]:3d}"
        print(f" {i+1:2d}  | ({point['coord'][0]:3d},{point['coord'][1]:3d}) | [{rgb_str}] | {point['depth']:12.2f} | {far_value:6.1f}m")
    
    # 计算统计信息
    depths = np.array([point['depth'] for point in boundary_points], dtype=np.float64)
    avg_depth = float(np.mean(depths))
    std_depth = float(np.std(depths))
    min_depth = float(np.min(depths))
    max_depth = float(np.max(depths))

    print(f"\n边界深度统计:")
    print(f"  原始值: {[float(d) for d in depths]}")
    print(f"  平均值: {avg_depth:.2f}")
    print(f"  标准差: {std_depth:.2f}")
    print(f"  范围: [{min_depth:.2f}, {max_depth:.2f}]")
    
    return boundary_points, avg_depth

def batch_analyze_multiple_far():
    """批量分析多个far值的情况"""
    
    # 配置要分析的文件对
    data_pairs = [
        {
            'far': 16,
            'rgb': 'C:\\Program Files (x86)\\Steam\\steamapps\\common\\Euro Truck Simulator 2\\bin\\win_x64\\cv_saved\\get_far_and_near\\pair1.png',
            'depth': 'C:\\Program Files (x86)\\Steam\\steamapps\\common\\Euro Truck Simulator 2\\bin\\win_x64\\cv_saved\\get_far_and_near\\pair1.npy'
        },
        {
            'far': 20,  # 换一个不同的far值
            'rgb': 'C:\\Program Files (x86)\\Steam\\steamapps\\common\\Euro Truck Simulator 2\\bin\\win_x64\\cv_saved\\get_far_and_near\\pair2.png',
            'depth': 'C:\\Program Files (x86)\\Steam\\steamapps\\common\\Euro Truck Simulator 2\\bin\\win_x64\\cv_saved\\get_far_and_near\\pair2.npy'
        }
        # 添加更多far值...
    ]
    
    all_results = []
    
    for data in data_pairs:
        if os.path.exists(data['rgb']) and os.path.exists(data['depth']):
            print(f"\n{'='*60}")
            print(f"处理: {os.path.basename(data['rgb'])}")
            print(f"Far值: {data['far']}m")
            print(f"{'='*60}")
            
            points, avg_depth = manual_boundary_selector(
                data['rgb'], data['depth'], data['far']
            )
            
            all_results.append({
                'far': data['far'],
                'points': points,
                'avg_depth': avg_depth,
                'rgb_file': data['rgb'],
                'depth_file': data['depth']
            })
        else:
            print(f"文件不存在: {data['rgb']} 或 {data['depth']}")
    
    # 输出最终汇总表格
    if all_results:
        print(f"\n{'='*80}")
        print("最终汇总结果")
        print(f"{'='*80}")
        print("Far值 |     平均深度值     | 物理距离 | 样本数量")
        print("-" * 55)
        
        calibration_data = []
        for result in all_results:
            print(f"{result['far']:5.1f}m | {result['avg_depth']:16.2f} | {result['far']:8.1f}m | {len(result['points']):8d}")
            calibration_data.append((result['avg_depth'], result['far']))
        
        print(f"\n校准数据对 (深度值, 物理距离):")
        for depth, dist in calibration_data:
            print(f"  ({depth:.2f}, {dist})")
        
        return calibration_data
    else:
        print("没有成功处理任何数据对")
        return []

# 单文件分析模式
def analyze_single_pair(rgb_path, depth_path, far_value):
    """分析单个文件对"""
    if os.path.exists(rgb_path) and os.path.exists(depth_path):
        points, avg_depth = manual_boundary_selector(rgb_path, depth_path, far_value)
        return [(avg_depth, far_value)]
    else:
        print("文件不存在")
        return []

if __name__ == "__main__":
    # 使用方式1: 批量分析多个far值
    # calibration_data = batch_analyze_multiple_far()
    
    # 使用方式2: 分析单个文件对
    rgb_path = "C:\\Program Files (x86)\\Steam\\steamapps\\common\\Euro Truck Simulator 2\\bin\\win_x64\\cv_saved\\get_far_and_near\\pair2.png"
    depth_path = "C:\\Program Files (x86)\\Steam\\steamapps\\common\\Euro Truck Simulator 2\\bin\\win_x64\\cv_saved\\get_far_and_near\\pair2.npy"
    far_value = 16
    
    calibration_data = analyze_single_pair(rgb_path, depth_path, far_value)
    
    if calibration_data:
        print(f"\n获得校准数据: {calibration_data}")
        print("你可以用这些数据来拟合深度转换公式!")