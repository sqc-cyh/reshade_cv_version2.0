import numpy as np

def test_matrix_type(extrinsic_matrix, depth_value=5.0):
    """
    测试外参矩阵类型
    
    参数:
    extrinsic_matrix: 3x4或4x4外参矩阵
    depth_value: 测试深度值
    
    返回:
    矩阵类型和测试结果
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
    
    # 相机位置
    camera_position = mat_4x4[:3, 3]
    
    # 计算从相机到点的向量
    vector_to_point = point_world - camera_position
    
    # 打印详细信息
    print(f"相机位置: {camera_position}")
    print(f"变换后的点: {point_world}")
    print(f"相机到点的向量: {vector_to_point}")
    
    # 检查点是否在相机前方
    # 假设Z轴是相机前方（这是常见约定）
    if abs(vector_to_point[2]) > 0.1:  # 避免除以零
        dot_product = np.dot(vector_to_point, [0, 0, 1])
        if dot_product > 0:
            return "c2w", "点在相机前方"
        else:
            return "w2c", "点在相机后方"
    else:
        return "不确定", "点与相机在同一平面"

# 测试GTAV矩阵
print("测试GTAV矩阵:")
extrinsic_list = [-0.9887291193008423, 0.13361068069934845, -0.06754860281944275, -4.838097095489502,
                  -0.1497117578983307, -0.8793095946311951, 0.4521072208881378, -1474.2041015625,
                  0.0010102150263264775, 0.45712441205978394, 0.8894021511077881, 31.194358825683594]
extrinsic_3x4 = np.array(extrinsic_list).reshape(3, 4)
matrix_type, result = test_matrix_type(extrinsic_3x4)
print(f"矩阵类型: {matrix_type}, 结果: {result}\n")

# 测试赛博朋克2077矩阵
print("测试赛博朋克2077矩阵:")
extrinsic_list_cp = [0.23052924871444702, -0.9699694514274597, -0.07756005227565765, -3958.417236328125,
                     0.9730654358863831, 0.22979581356048584, 0.018374769017100334, -6502.53173828125,
                     7.450580596923828e-09, -0.07970692217350006, 0.9968183636665344, 79.145263671875]
extrinsic_3x4_cp = np.array(extrinsic_list_cp).reshape(3, 4)
matrix_type_cp, result_cp = test_matrix_type(extrinsic_3x4_cp)
print(f"矩阵类型: {matrix_type_cp}, 结果: {result_cp}")