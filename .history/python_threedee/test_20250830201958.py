import numpy as np

# 示例矩阵
matrix = np.array([
    [0.23052924871444702, -0.9699694514274597, -0.07756005227565765],
    [0.9730654358863831, 0.22979581356048584, 0.018374769017100334],
    [7.450580596923828e-09, -0.07970692217350006, 0.9968183636665344]
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
