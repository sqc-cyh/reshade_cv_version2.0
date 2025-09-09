import numpy as np
import json
from PIL import Image
import open3d as o3d
import math
import os

# --- 配置 ---
# 请将这里改为您存放数据的文件夹路径
DATA_DIR = "F:\data\备份\data" 
# 体素下采样：这是一个非常重要的步骤，可以减少点云密度，提高可视化性能
# 值越大，点云越稀疏。您可以根据场景大小调整这个值。
VOXEL_SIZE = 0.01 

def reconstruct_single_frame(rgb_path, depth_path, json_path):
    """
    这是一个将单帧重建流程封装起来的函数。
    输入文件路径，返回一个Open3D点云对象。
    """
    # 1. 加载数据
    with open(json_path, 'r') as f:
        camera_params = json.load(f)
    extrinsic_list = camera_params['extrinsic_cam2world']
    fov_v_degrees = camera_params['fov_v_degrees']

    rgb_pil = Image.open(rgb_path)
    depth_map = np.load(depth_path)
    rgb_np = np.array(rgb_pil)
    height, width, _ = rgb_np.shape

    # 2. 修正相机外参 (c2w 矩阵)
    c2w_gta_original = np.array(extrinsic_list).reshape(3, 4)
    c2w_gta_original = np.vstack([c2w_gta_original, [0, 0, 0, 1]])
    
    conversion_matrix = np.array([
        [1,  0,  0, 0],
        [0,  0,  1, 0],
        [0, -1,  0, 0],
        [0,  0,  0, 1]
    ])
    
    c2w_corrected = c2w_gta_original @ conversion_matrix

    # 3. 计算相机内参矩阵 (K)
    fov_v_rad = math.radians(fov_v_degrees)
    fy = height / (2 * math.tan(fov_v_rad / 2))
    fx = fy 
    cx, cy = width / 2, height / 2
    K = np.array([[fx, 0, cx], [0, fy, cy], [0, 0, 1]])

    # 4. 创建点云
    u, v = np.meshgrid(np.arange(width), np.arange(height))
    u_flat, v_flat = u.flatten(), v.flatten()
    depth_flat = depth_map.flatten()
    colors_flat = rgb_np.reshape(-1, 3)

    x_cam = (u_flat - cx) * depth_flat / fx
    y_cam = (v_flat - cy) * depth_flat / fy
    z_cam = depth_flat
    
    points_camera_space = np.vstack((x_cam, y_cam, z_cam)).T
    points_cam_homogeneous = np.hstack([points_camera_space, np.ones((points_camera_space.shape[0], 1))])
    points_world_space = (c2w_corrected @ points_cam_homogeneous.T).T[:, :3]

    # 5. 创建Open3D点云对象
    pcd = o3d.geometry.PointCloud()
    pcd.points = o3d.utility.Vector3dVector(points_world_space)
    pcd.colors = o3d.utility.Vector3dVector(colors_flat / 255.0)
    
    return pcd


def main():
    # 获取所有json文件的列表，并排序以保证处理顺序
    json_files = sorted([f for f in os.listdir(DATA_DIR) if f.endswith('.json')])
    
    if not json_files:
        print(f"错误：在文件夹 '{DATA_DIR}' 中没有找到任何 .json 文件。")
        return

    all_point_clouds = []
    print(f"找到了 {len(json_files)} 帧数据，开始处理...")

    # 循环处理每一帧
    for i, json_file in enumerate(json_files):
        base_name = os.path.splitext(json_file)[0]
        print(f"[{i+1}/{len(json_files)}] 正在处理: {base_name}...")
        
        rgb_path = os.path.join(DATA_DIR, base_name + '.png')
        depth_path = os.path.join(DATA_DIR, base_name + '.npy')
        json_path = os.path.join(DATA_DIR, json_file)

        # 检查文件是否存在
        if not all(os.path.exists(p) for p in [rgb_path, depth_path]):
            print(f"   警告: 找不到对应的 .png 或 .npy 文件，跳过 {base_name}")
            continue

        # 重建单帧点云
        pcd = reconstruct_single_frame(rgb_path, depth_path, json_path)
        
        # (可选但强烈推荐) 进行体素下采样
        pcd_downsampled = pcd.voxel_down_sample(voxel_size=VOXEL_SIZE)
        
        all_point_clouds.append(pcd_downsampled)

    print("\n所有帧处理完毕！准备可视化...")

    # 创建一个世界坐标系的可视化参考
    coordinate_frame = o3d.geometry.TriangleMesh.create_coordinate_frame(size=5.0, origin=[0, 0, 0])
    all_point_clouds.append(coordinate_frame)
    
    # 将所有点云一起可视化
    o3d.visualization.draw_geometries(all_point_clouds)


if __name__ == '__main__':
    main()