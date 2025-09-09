import numpy as np

# 示例矩阵
matrix = np.array([
    [-0.6396541595458984, 0.7686310410499573, 0.006987080909311771],
    [-0.7137196063995361, -0.5972827076911926, 0.3658655285835266],
    [0.28538888692855835, 0.22904060781002045, 0.9306414127349854]
])

# 计算转置矩阵
matrix_T = matrix.T

# 计算矩阵与转置矩阵的乘积
product = np.dot(matrix_T, matrix)

# 生成单位矩阵
identity_matrix = np.identity(matrix.shape[0])

# 计算误差
error = product - identity_matrix

# 输出误差
print("误差矩阵：")
print(error)

# 检查乘积是否接近单位矩阵
if np.allclose(product, identity_matrix, atol=1e-6):  # 允许 1e-6 的误差
    print("矩阵接近正交。")
else:
    print("矩阵不接近正交。")
