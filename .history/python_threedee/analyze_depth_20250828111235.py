import numpy as np

# 读取文件
arr = np.load(""F:\SteamLibrary\steamapps\common\Grand Theft Auto V\cv_saved\38.npy"")

# 打印数组
print(arr)

# 查看形状和数据类型
print("Shape:", arr.shape)
print("Dtype:", arr.dtype)

# 如果数组很大，可以只看一部分
print(arr[:10])         # 前 10 个元素
print(arr[0, :5])       # 第一行前 5 个
