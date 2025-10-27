import numpy as np
import matplotlib.pyplot as plt
from PIL import Image
import os

def calibrate_depth_formula(calibration_data):
    """
    根据校准数据计算深度转换公式的参数
    
    参数:
        calibration_data: 列表，每个元素为 (depth_value, physical_distance, far_value)
    
    返回:
        depth_min, depth_max, near, far: 转换公式参数
    """
    
    print("开始校准深度转换公式...")
    
    # 提取深度值和物理距离
    depth_values = [data[0] for data in calibration_data]
    physical_distances = [data[1] for data in calibration_data]
    
    # 估计深度值范围
    depth_min = min(depth_values) * 0.9  # 留一些余量
    depth_max = max(depth_values) * 1.1
    
    print(f"估计深度值范围: [{depth_min:.2f}, {depth_max:.2f}]")
    
    # 归一化深度值
    z_norm_values = [(dv - depth_min) / (depth_max - depth_min) for dv in depth_values]
    
    # 由于我们有多个far值，我们需要找到游戏内部的near值
    # 使用优化方法找到最佳的near和far_internal
    
    from scipy.optimize import minimize
    
    def objective(params):
        near, far_internal = params
        error = 0
        for z_norm, physical in zip(z_norm_values, physical_distances):
            # 反向Z深度公式
            predicted = (near * far_internal) / (far_internal - z_norm * (far_internal - near))
            error += (predicted - physical) ** 2
        return error
    
    # 初始猜测
    initial_guess = [0.1, 1000.0]  # [near, far_internal]
    
    # 边界约束
    bounds = [(0.01, 1.0), (100.0, 10000.0)]
    
    # 优化
    result = minimize(objective, initial_guess, bounds=bounds, method='L-BFGS-B')
    
    if result.success:
        near_opt, far_internal_opt = result.x
        print(f"优化成功!")
        print(f"游戏内部 near: {near_opt:.6f}")
        print(f"游戏内部 far: {far_internal_opt:.2f}")
        
        # 验证结果
        print("\n校准验证:")
        for i, (depth_val, physical, far_set) in enumerate(calibration_data):
            z_norm = (depth_val - depth_min) / (depth_max - depth_min)
            predicted = (near_opt * far_internal_opt) / (far_internal_opt - z_norm * (far_internal_opt - near_opt))
            print(f"点{i+1}: 深度值={depth_val:.2f}, 实际距离={physical}米, 预测距离={predicted:.2f}米, 误差={abs(predicted-physical):.4f}米")
        
        return depth_min, depth_max, near_opt, far_internal_opt
    else:
        print("优化失败，使用备选方法...")
        # 备选方法：使用两个点直接计算
        if len(calibration_data) >= 2:
            return simple_calibration(calibration_data, depth_min, depth_max)
        else:
            return None

def simple_calibration(calibration_data, depth_min, depth_max):
    """简单的两点校准方法"""
    print("使用两点校准方法...")
    
    # 取最近和最远的两个点
    sorted_data = sorted(calibration_data, key=lambda x: x[1])  # 按物理距离排序
    near_data = sorted_data[0]  # 最近的点
    far_data = sorted_data[-1]  # 最远的点
    
    z_norm_near = (near_data[0] - depth_min) / (depth_max - depth_min)
    z_norm_far = (far_data[0] - depth_min) / (depth_max - depth_min)
    
    # 解方程组
    # d1 = (n*f)/(f - z1*(f-n))
    # d2 = (n*f)/(f - z2*(f-n))
    
    # 简化：假设我们知道其中一个点对应near平面
    near_guess = near_data[1]  # 最近点的物理距离作为near的估计
    far_guess = far_data[1]   # 最远点的物理距离作为far的估计
    
    print(f"简单校准结果: near={near_guess:.6f}, far={far_guess:.2f}")
    return depth_min, depth_max, near_guess, far_guess

def create_depth_converter(depth_min, depth_max, near, far_internal):
    """创建深度转换函数"""
    def depth_to_distance(depth_value):
        # 归一化深度值
        z_norm = (depth_value - depth_min) / (depth_max - depth_min)
        # 反向Z深度到物理距离
        if z_norm >= 1.0:
            return float('inf')
        return (near * far_internal) / (far_internal - z_norm * (far_internal - near))
    
    return depth_to_distance

# 主程序
if __name__ == "__main__":

    
    calibration_data = [[16609431, 30, 30], [16608706, 16, 16]]
    

    print(f"\n收集到 {len(calibration_data)} 个校准点")
    for i, (depth, dist, far) in enumerate(calibration_data):
        print(f"点{i+1}: 深度值={depth:.2f}, 物理距离={dist}米 (far={far})")
    
    # 校准公式
    params = calibrate_depth_formula(calibration_data)
    if params:
        depth_min, depth_max, near, far_internal = params
        
        # 创建转换函数
        depth_to_distance = create_depth_converter(depth_min, depth_max, near, far_internal)
        
        print(f"\n=== 最终深度转换公式 ===")
        print(f"深度值范围: [{depth_min:.2f}, {depth_max:.2f}]")
        print(f"游戏内部参数: near={near:.6f}, far={far_internal:.2f}")
        print(f"使用方式: distance = depth_to_distance(depth_value)")
        
        # 测试一些典型值
        print(f"\n测试转换:")
        test_depths = [depth_min, np.mean([depth_min, depth_max]), depth_max*0.99]
        for depth_val in test_depths:
            distance = depth_to_distance(depth_val)
            print(f"深度值 {depth_val:.2f} -> 物理距离 {distance:.2f} 米")
    
