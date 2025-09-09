import numpy as np
import struct

def get_center_depth(npy_file_path):
    """
    加载包含原始vi整数的.npy文件，并提取中心区域的深度值。
    """
    # 加载包含原始 uint32 'vi' 值的数据
    # 注意：即使 vi 在 C++ 中是 uint64_t，它实际上只填充了低 32 位
    depth_vi_map = np.load(npy_file_path).astype(np.uint32)

    # 获取图像的高度和宽度
    h, w = depth_vi_map.shape
    center_y, center_x = h // 2, w // 2

    # --- 方法 1: 只取最中心的一个像素 ---
    center_vi_value = depth_vi_map[center_y, center_x]

    # --- 方法 2: 取中心 10x10 区域的中位数 (更稳健，推荐!) ---
    # 这可以避免因单个像素噪声或边缘效应造成的误差
    patch_size = 10
    half_patch = patch_size // 2
    
    center_patch = depth_vi_map[center_y - half_patch : center_y + half_patch,
                                center_x - half_patch : center_x + half_patch]
    
    # 使用中位数而不是平均值，以更好地抵抗异常值（例如，如果准星边缘有其他物体）
    median_vi_value = np.median(center_patch)

    # --- 将整数 vi 值重新解释为浮点数 ---
    # 这是 Python 中等效于 C++ 的 memcpy/bit_cast 的方法
    # 我们使用 struct 来进行打包和解包
    # '<I' 表示小端无符号32位整数, '<f' 表示小端32位浮点数
    def vi_to_float(vi):
        # 将整数打包成4个字节
        packed = struct.pack('<I', vi)
        # 将这4个字节解包成一个浮点数
        return struct.unpack('<f', packed)[0]

    center_float_depth = vi_to_float(int(center_vi_value))
    median_float_depth = vi_to_float(int(median_vi_value))

    print(f"文件: {npy_file_path}")
    print(f"中心像素vi值: {center_vi_value}, 对应的浮点深度: {center_float_depth:.6f}")
    print(f"中心区域中位数vi值: {int(median_vi_value)}, 对应的浮点深度: {median_float_depth:.6f}")
    
    return (int(median_vi_value), median_float_depth)


# --- 使用示例 ---
# 假设您在距离目标 20 米处捕获了文件
distance_meters = 187.27
vi_value, float_depth = get_center_depth('F:\SteamLibrary\steamapps\common\Grand Theft Auto V\cv_saved\GTAV_2025-08-29_1173113750_depth.npy')

# 现在您可以开始记录您的数据对了
print(f"\n数据点: (距离: {distance_meters}m, 浮点深度: {float_depth:.6f})")