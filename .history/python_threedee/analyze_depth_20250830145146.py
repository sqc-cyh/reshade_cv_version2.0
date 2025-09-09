def check_coordinate_system(extrinsic_matrix):
    # 提取旋转部分
    rotation = extrinsic_matrix[:3, :3]
    
    # 检查各轴方向
    x_axis = rotation[:, 0]  # 右方向
    y_axis = rotation[:, 1]  # 上方向
    z_axis = rotation[:, 2]  # 前方向
    
    print(f"右方向 (X轴): {x_axis}")
    print(f"上方向 (Y轴): {y_axis}")
    print(f"前方向 (Z轴): {z_axis}")
    
    # 检查坐标系手性（左手系或右手系）
    # 通过计算叉积: X × Y 应该等于 Z（右手系）或 -Z（左手系）
    cross_product = np.cross(x_axis, y_axis)
    dot_product = np.dot(cross_product, z_axis)
    
    if dot_product > 0:
        print("右手坐标系")
    else:
        print("左手坐标系")
    
    return x_axis, y_axis, z_axis