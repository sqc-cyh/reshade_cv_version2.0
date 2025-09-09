import numpy as np

def determine_matrix_type(extrinsic_matrix, depth_value=1.0):
    """
    确定外参矩阵是c2w还是w2c
    
    参数:
    extrinsic_matrix: 3x4或4x4外参矩阵
    depth_value: 测试用的深度值
    
    返回:
    matrix_type: "c2w" 或 "w2c"
    """
    # 确保是4x4矩阵
    if extrinsic_matrix.shape == (3, 4):
        mat_4x4 = np.eye(4)
        mat_4x4[:3, :4] = extrinsic_matrix
    else:
        mat_4x4 = extrinsic_matrix
    
    # 测试点：相机前方的点 (0, 0, depth_value)
    point_camera = np.array([0, 0, depth_value, 1])
    
    # 使用矩阵变换到世界坐标系
    point_world = np.dot(mat_4x4, point_camera)[:3]
    
    # 如果变换后的点位于相机前方，可能是c2w矩阵
    # 如果变换后的点位于相机后方，可能是w2c矩阵
    camera_position = mat_4x4[:3, 3]
    vector_to_point = point_world - camera_position
    
    # 检查点是否在相机前方
    if vector_to_point[2] > 0:  # 假设Z轴是相机前方
        return "c2w"
    else:
        return "w2c"

# 使用您提供的GTAV矩阵进行测试
extrinsic_list = [-0.9887291193008423, 0.13361068069934845, -0.06754860281944275, -4.838097095489502,
                  -0.1497117578983307, -0.8793095946311951, 0.4521072208881378, -1474.2041015625,
                  0.0010102150263264775, 0.45712441205978394, 0.8894021511077881, 31.194358825683594]

extrinsic_3x4 = np.array(extrinsic_list).reshape(3, 4)
matrix_type = determine_matrix_type(extrinsic_3x4)
print(f"矩阵类型: {matrix_type}")