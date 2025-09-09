import numpy as np
import json

def compare_game_data(gtav_file, cyberpunk_file):
    # 加载GTAV数据
    with open(gtav_file, 'r') as f:
        gtav_data = json.load(f)
    
    # 加载赛博朋克2077数据
    with open(cyberpunk_file, 'r') as f:
        cp_data = json.load(f)
    
    # 比较外参矩阵
    gtav_extrinsic = np.array(gtav_data['extrinsic_cam2world']).reshape(3, 4)
    cp_extrinsic = np.array(cp_data['extrinsic_cam2world']).reshape(3, 4)
    
    print("GTAV外参矩阵:")
    print(gtav_extrinsic)
    print("\n赛博朋克2077外参矩阵:")
    print(cp_extrinsic)
    
    # 比较平移向量
    print(f"\nGTAV平移向量: {gtav_extrinsic[:, 3]}")
    print(f"赛博朋克2077平移向量: {cp_extrinsic[:, 3]}")
    
    # 比较旋转矩阵部分
    gtav_rotation = gtav_extrinsic[:, :3]
    cp_rotation = cp_extrinsic[:, :3]
    
    print(f"\nGTAV旋转矩阵行列式: {np.linalg.det(gtav_rotation)}")
    print(f"赛博朋克2077旋转矩阵行列式: {np.linalg.det(cp_rotation)}")
    
    # 检查是否是正交矩阵
    gtav_ortho_test = np.dot(gtav_rotation.T, gtav_rotation)
    cp_ortho_test = np.dot(cp_rotation.T, cp_rotation)
    
    print(f"\nGTAV旋转矩阵正交性检查:")
    print(gtav_ortho_test)
    print(f"\n赛博朋克2077旋转矩阵正交性检查:")
    print(cp_ortho_test)
    
    # 比较FoV
    print(f"\nGTAV FoV: {gtav_data.get('fov_v_degrees', 'N/A')}")
    print(f"赛博朋克2077 FoV: {cp_data.get('fov_v_degrees', 'N/A')}")

# 使用示例
compare_game_data('F:\SteamLibrary\steamapps\common\Grand Theft Auto V\cv_saved\GTAV_2025-08-30_2057837496_meta.json', 'E:\steam\steamapps\common\Cyberpunk 2077\bin\x64\cv_saved\actions_2025-08-28_288796023\frame_000000_camera.json')