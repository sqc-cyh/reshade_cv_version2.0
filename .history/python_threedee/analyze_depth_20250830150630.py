import numpy as np
import json
import matplotlib.pyplot as plt
from mpl_toolkits.mplot3d import Axes3D

def load_camera_data(json_file):
    """加载相机JSON文件并提取外参矩阵"""
    with open(json_file, 'r') as f:
        data = json.load(f)
    
    extrinsic_list = data['extrinsic_cam2world']
    extrinsic_3x4 = np.array(extrinsic_list).reshape((3, 4))
    
    # 构建4x4齐次坐标矩阵
    cam2world = np.eye(4)
    cam2world[:3, :4] = extrinsic_3x4
    
    # 获取其他信息
    fov = data.get('fov_v_degrees', data.get('fov_h_degrees', None))
    img_size = (data.get('img_w', 0), data.get('img_h', 0))
    
    return cam2world, fov, img_size, json_file

def analyze_coordinate_system(cam2world, name):
    """分析坐标系方向"""
    # 提取旋转矩阵和平移向量
    rotation = cam2world[:3, :3]
    translation = cam2world[:3, 3]
    
    # 提取各轴方向
    x_axis = rotation[:, 0]  # 右方向
    y_axis = rotation[:, 1]  # 上方向
    z_axis = rotation[:, 2]  # 前方向
    
    print(f"\n=== {name} 坐标系分析 ===")
    print(f"相机位置: {translation}")
    print(f"右方向 (X轴): {x_axis}")
    print(f"上方向 (Y轴): {y_axis}")
    print(f"前方向 (Z轴): {z_axis}")
    
    # 检查坐标系手性
    cross_product = np.cross(x_axis, y_axis)
    dot_product = np.dot(cross_product, z_axis)
    
    if dot_product > 0:
        handiness = "右手坐标系"
    else:
        handiness = "左手坐标系"
    
    print(f"坐标系手性: {handiness}")
    
    # 检查各轴是否正交
    ortho_xy = np.abs(np.dot(x_axis, y_axis))
    ortho_xz = np.abs(np.dot(x_axis, z_axis))
    ortho_yz = np.abs(np.dot(y_axis, z_axis))
    
    print(f"X-Y轴正交性: {ortho_xy:.6f} (接近0表示正交)")
    print(f"X-Z轴正交性: {ortho_xz:.6f} (接近0表示正交)")
    print(f"Y-Z轴正交性: {ortho_yz:.6f} (接近0表示正交)")
    
    # 检查旋转矩阵是否为标准正交矩阵
    rotation_det = np.linalg.det(rotation)
    print(f"旋转矩阵行列式: {rotation_det:.6f} (应为±1)")
    
    return {
        'rotation': rotation,
        'translation': translation,
        'x_axis': x_axis,
        'y_axis': y_axis,
        'z_axis': z_axis,
        'handiness': handiness
    }

def compare_coordinate_systems(gtav_data, cp_data):
    """比较两个坐标系"""
    print("\n=== 坐标系比较 ===")
    
    # 比较轴方向
    x_similarity = np.abs(np.dot(gtav_data['x_axis'], cp_data['x_axis']))
    y_similarity = np.abs(np.dot(gtav_data['y_axis'], cp_data['y_axis']))
    z_similarity = np.abs(np.dot(gtav_data['z_axis'], cp_data['z_axis']))
    
    print(f"X轴方向相似度: {x_similarity:.6f} (接近1表示方向相似)")
    print(f"Y轴方向相似度: {y_similarity:.6f} (接近1表示方向相似)")
    print(f"Z轴方向相似度: {z_similarity:.6f} (接近1表示方向相似)")
    
    # 比较坐标系手性
    handiness_match = gtav_data['handiness'] == cp_data['handiness']
    print(f"坐标系手性一致: {handiness_match}")
    
    # 比较平移向量尺度
    gtav_scale = np.linalg.norm(gtav_data['translation'])
    cp_scale = np.linalg.norm(cp_data['translation'])
    scale_ratio = max(gtav_scale, cp_scale) / min(gtav_scale, cp_scale)
    
    print(f"GTAV平移向量尺度: {gtav_scale:.2f}")
    print(f"赛博朋克2077平移向量尺度: {cp_scale:.2f}")
    print(f"尺度比例: {scale_ratio:.2f}")
    
    # 确定坐标系是否一致
    axes_similarity = min(x_similarity, y_similarity, z_similarity)
    
    if axes_similarity > 0.9 and handiness_match and scale_ratio < 10:
        print("结论: 两个游戏的坐标系基本一致")
    else:
        print("结论: 两个游戏的坐标系不一致")
        
        if axes_similarity <= 0.9:
            print("  - 轴方向不一致")
        if not handiness_match:
            print("  - 坐标系手性不一致")
        if scale_ratio >= 10:
            print("  - 平移向量尺度差异较大")

def visualize_coordinate_system(cam2world, name, ax):
    """可视化坐标系"""
    # 相机位置
    camera_pos = cam2world[:3, 3]
    
    # 提取各轴方向
    rotation = cam2world[:3, :3]
    x_axis = rotation[:, 0] * 5  # 缩放以便可视化
    y_axis = rotation[:, 1] * 5
    z_axis = rotation[:, 2] * 5
    
    # 绘制相机位置
    ax.scatter(*camera_pos, c='r', marker='o', s=100, label=f'{name} 相机')
    
    # 绘制坐标轴
    ax.quiver(*camera_pos, *x_axis, color='r', label=f'{name} X轴')
    ax.quiver(*camera_pos, *y_axis, color='g', label=f'{name} Y轴')
    ax.quiver(*camera_pos, *z_axis, color='b', label=f'{name} Z轴')
    
    # 添加标签
    ax.text(*camera_pos, f'{name} 相机', fontsize=8)

def main():
    # 文件路径
    gtav_file = "F:\\SteamLibrary\\steamapps\\common\\Grand Theft Auto V\\cv_saved\\GTAV_2025-08-30_2057837496_meta.json"
    cp_file = "E:\\steam\\steamapps\\common\\Cyberpunk 2077\\bin\\x64\\cv_saved\\actions_2025-08-28_288796023\\frame_000000_camera.json"
    
    try:
        # 加载GTAV相机数据
        gtav_cam2world, gtav_fov, gtav_img_size, gtav_name = load_camera_data(gtav_file)
        gtav_data = analyze_coordinate_system(gtav_cam2world, "GTAV")
        
        # 加载赛博朋克2077相机数据
        cp_cam2world, cp_fov, cp_img_size, cp_name = load_camera_data(cp_file)
        cp_data = analyze_coordinate_system(cp_cam2world, "赛博朋克2077")
        
        # 比较两个坐标系
        compare_coordinate_systems(gtav_data, cp_data)
        
        # 可视化
        fig = plt.figure(figsize=(12, 10))
        ax = fig.add_subplot(111, projection='3d')
        
        visualize_coordinate_system(gtav_cam2world, "GTAV", ax)
        visualize_coordinate_system(cp_cam2world, "CP2077", ax)
        
        # 设置图表属性
        ax.set_xlabel('X轴')
        ax.set_ylabel('Y轴')
        ax.set_zlabel('Z轴')
        ax.set_title('相机坐标系比较')
        ax.legend()
        
        # 调整视角以便更好地观察
        ax.view_init(elev=20, azim=30)
        
        plt.tight_layout()
        plt.show()
        
    except Exception as e:
        print(f"错误: {e}")
        print("请检查文件路径是否正确")

if __name__ == "__main__":
    main()