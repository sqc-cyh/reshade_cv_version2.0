import numpy as np
import json
import math

def load_camera_data(json_file):
    """加载相机JSON文件并提取外参矩阵"""
    with open(json_file, 'r') as f:
        data = json.load(f)
    
    extrinsic_list = data['extrinsic_cam2world']
    extrinsic_3x4 = np.array(extrinsic_list).reshape((3, 4))
    
    # 构建4x4齐次坐标矩阵
    cam2world = np.eye(4)
    cam2world[:3, :4] = extrinsic_3x4
    
    return cam2world, data.get('fov_v_degrees', 50.0)

def rotation_matrix_to_euler_angles_zyx(R):
    """将旋转矩阵转换为ZYX欧拉角（偏航-俯仰-滚转）"""
    sy = math.sqrt(R[0,0] * R[0,0] + R[1,0] * R[1,0])
    
    singular = sy < 1e-6
    
    if not singular:
        x = math.atan2(R[2,1], R[2,2])
        y = math.atan2(-R[2,0], sy)
        z = math.atan2(R[1,0], R[0,0])
    else:
        x = math.atan2(-R[1,2], R[1,1])
        y = math.atan2(-R[2,0], sy)
        z = 0
        
    return np.array([x, y, z])

def rotation_matrix_to_euler_angles_xyz(R):
    """将旋转矩阵转换为XYZ欧拉角（滚转-俯仰-偏航）"""
    sy = math.sqrt(R[0,0] * R[0,0] + R[0,1] * R[0,1])
    
    singular = sy < 1e-6
    
    if not singular:
        x = math.atan2(R[2,1], R[2,2])
        y = math.atan2(-R[2,0], sy)
        z = math.atan2(R[1,0], R[0,0])
    else:
        x = math.atan2(-R[1,2], R[1,1])
        y = math.atan2(-R[2,0], sy)
        z = 0
        
    return np.array([x, y, z])

def rotation_matrix_to_euler_angles_yxz(R):
    """将旋转矩阵转换为YXZ欧拉角（俯仰-滚转-偏航）"""
    sz = math.sqrt(R[0,0] * R[0,0] + R[0,1] * R[0,1])
    
    singular = sz < 1e-6
    
    if not singular:
        x = math.atan2(R[2,1], R[2,2])
        y = math.atan2(-R[2,0], sz)
        z = math.atan2(R[1,0], R[0,0])
    else:
        x = math.atan2(-R[1,2], R[1,1])
        y = math.atan2(-R[2,0], sz)
        z = 0
        
    return np.array([x, y, z])

def rotation_matrix_to_euler_angles_zxy(R):
    """将旋转矩阵转换为ZXY欧拉角（偏航-滚转-俯仰）"""
    sy = math.sqrt(R[0,0] * R[0,0] + R[1,0] * R[1,0])
    
    singular = sy < 1e-6
    
    if not singular:
        x = math.atan2(R[2,1], R[2,2])
        y = math.atan2(-R[2,0], sy)
        z = math.atan2(R[1,0], R[0,0])
    else:
        x = math.atan2(-R[1,2], R[1,1])
        y = math.atan2(-R[2,0], sy)
        z = 0
        
    return np.array([x, y, z])

def euler_angles_to_rotation_matrix_zyx(euler):
    """将ZYX欧拉角转换为旋转矩阵"""
    x, y, z = euler
    
    Rx = np.array([[1, 0, 0],
                   [0, math.cos(x), -math.sin(x)],
                   [0, math.sin(x), math.cos(x)]])
    
    Ry = np.array([[math.cos(y), 0, math.sin(y)],
                   [0, 1, 0],
                   [-math.sin(y), 0, math.cos(y)]])
    
    Rz = np.array([[math.cos(z), -math.sin(z), 0],
                   [math.sin(z), math.cos(z), 0],
                   [0, 0, 1]])
    
    return np.dot(Rz, np.dot(Ry, Rx))

def euler_angles_to_rotation_matrix_xyz(euler):
    """将XYZ欧拉角转换为旋转矩阵"""
    x, y, z = euler
    
    Rx = np.array([[1, 0, 0],
                   [0, math.cos(x), -math.sin(x)],
                   [0, math.sin(x), math.cos(x)]])
    
    Ry = np.array([[math.cos(y), 0, math.sin(y)],
                   [0, 1, 0],
                   [-math.sin(y), 0, math.cos(y)]])
    
    Rz = np.array([[math.cos(z), -math.sin(z), 0],
                   [math.sin(z), math.cos(z), 0],
                   [0, 0, 1]])
    
    return np.dot(Rx, np.dot(Ry, Rz))

def euler_angles_to_rotation_matrix_yxz(euler):
    """将YXZ欧拉角转换为旋转矩阵"""
    x, y, z = euler
    
    Rx = np.array([[1, 0, 0],
                   [0, math.cos(x), -math.sin(x)],
                   [0, math.sin(x), math.cos(x)]])
    
    Ry = np.array([[math.cos(y), 0, math.sin(y)],
                   [0, 1, 0],
                   [-math.sin(y), 0, math.cos(y)]])
    
    Rz = np.array([[math.cos(z), -math.sin(z), 0],
                   [math.sin(z), math.cos(z), 0],
                   [0, 0, 1]])
    
    return np.dot(Ry, np.dot(Rx, Rz))

def euler_angles_to_rotation_matrix_zxy(euler):
    """将ZXY欧拉角转换为旋转矩阵"""
    x, y, z = euler
    
    Rx = np.array([[1, 0, 0],
                   [0, math.cos(x), -math.sin(x)],
                   [0, math.sin(x), math.cos(x)]])
    
    Ry = np.array([[math.cos(y), 0, math.sin(y)],
                   [0, 1, 0],
                   [-math.sin(y), 0, math.cos(y)]])
    
    Rz = np.array([[math.cos(z), -math.sin(z), 0],
                   [math.sin(z), math.cos(z), 0],
                   [0, 0, 1]])
    
    return np.dot(Rz, np.dot(Rx, Ry))

def test_euler_angle_orders(cam2world):
    """测试不同的欧拉角顺序"""
    # 提取旋转部分
    R = cam2world[:3, :3]
    
    print("原始旋转矩阵:")
    print(R)
    
    # 测试不同的欧拉角顺序
    orders = ['ZYX', 'XYZ', 'YXZ', 'ZXY']
    extract_funcs = {
        'ZYX': rotation_matrix_to_euler_angles_zyx,
        'XYZ': rotation_matrix_to_euler_angles_xyz,
        'YXZ': rotation_matrix_to_euler_angles_yxz,
        'ZXY': rotation_matrix_to_euler_angles_zxy
    }
    build_funcs = {
        'ZYX': euler_angles_to_rotation_matrix_zyx,
        'XYZ': euler_angles_to_rotation_matrix_xyz,
        'YXZ': euler_angles_to_rotation_matrix_yxz,
        'ZXY': euler_angles_to_rotation_matrix_zxy
    }
    
    best_order = None
    best_error = float('inf')
    
    for order in orders:
        try:
            # 提取欧拉角
            euler = extract_funcs[order](R)
            euler_deg = np.degrees(euler)
            
            # 重建旋转矩阵
            R_reconstructed = build_funcs[order](euler)
            
            # 计算重建误差
            error = np.linalg.norm(R - R_reconstructed)
            
            print(f"\n{order}顺序:")
            print(f"  欧拉角 (弧度): {euler}")
            print(f"  欧拉角 (度): {euler_deg}")
            print(f"  重建误差: {error}")
            
            # 检查是否是最佳顺序
            if error < best_error:
                best_error = error
                best_order = order
                
        except Exception as e:
            print(f"\n{order}顺序: 错误 - {e}")
    
    print(f"\n最佳顺序: {best_order}, 误差: {best_error}")
    
    return best_order

def check_coordinate_system(cam2world):
    """检查坐标系的性质"""
    # 提取旋转部分
    R = cam2world[:3, :3]
    
    print("坐标系检查:")
    print(f"旋转矩阵行列式: {np.linalg.det(R)}")
    
    # 检查是否是正交矩阵
    ortho_test = np.dot(R.T, R)
    print(f"正交性检查 (R^T * R):")
    print(ortho_test)
    
    # 检查各轴是否正交
    x_axis = R[:, 0]
    y_axis = R[:, 1]
    z_axis = R[:, 2]
    
    dot_xy = np.dot(x_axis, y_axis)
    dot_xz = np.dot(x_axis, z_axis)
    dot_yz = np.dot(y_axis, z_axis)
    
    print(f"X-Y轴点积: {dot_xy} (接近0表示正交)")
    print(f"X-Z轴点积: {dot_xz} (接近0表示正交)")
    print(f"Y-Z轴点积: {dot_yz} (接近0表示正交)")
    
    # 检查各轴长度
    len_x = np.linalg.norm(x_axis)
    len_y = np.linalg.norm(y_axis)
    len_z = np.linalg.norm(z_axis)
    
    print(f"X轴长度: {len_x} (应为1)")
    print(f"Y轴长度: {len_y} (应为1)")
    print(f"Z轴长度: {len_z} (应为1)")

def main():
    # GTAV文件路径
    gtav_file = "E:\steam\steamapps\common\Cyberpunk 2077\bin\x64\cv_saved\actions_2025-08-28_288796023\frame_000000_camera.json"
    
    try:
        # 加载相机数据
        cam2world, fov_v = load_camera_data(gtav_file)
        
        print(f"相机位置: {cam2world[:3, 3]}")
        print(f"FoV: {fov_v}度")
        
        # 检查坐标系性质
        check_coordinate_system(cam2world)
        
        # 测试不同的欧拉角顺序
        best_order = test_euler_angle_orders(cam2world)
        
        print(f"\n根据分析，GTAV最可能使用的欧拉角顺序是: {best_order}")
        
        # 提取最佳顺序的欧拉角
        if best_order == 'ZYX':
            euler = rotation_matrix_to_euler_angles_zyx(cam2world[:3, :3])
        elif best_order == 'XYZ':
            euler = rotation_matrix_to_euler_angles_xyz(cam2world[:3, :3])
        elif best_order == 'YXZ':
            euler = rotation_matrix_to_euler_angles_yxz(cam2world[:3, :3])
        elif best_order == 'ZXY':
            euler = rotation_matrix_to_euler_angles_zxy(cam2world[:3, :3])
        else:
            print("无法确定最佳欧拉角顺序")
            return
        
        euler_deg = np.degrees(euler)
        print(f"\n最佳顺序的欧拉角 (弧度): {euler}")
        print(f"最佳顺序的欧拉角 (度): {euler_deg}")
        
    except Exception as e:
        print(f"错误: {e}")
        print("请检查文件路径是否正确")

if __name__ == "__main__":
    main()