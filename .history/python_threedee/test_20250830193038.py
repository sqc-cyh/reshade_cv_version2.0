import numpy as np
import open3d as o3d
import json
from PIL import Image

# Update your file paths here
rgb_image_path = r'F:\SteamLibrary\steamapps\common\Grand Theft Auto V\cv_saved\GTAV_2025-08-30_492686459_RGB.png'
depth_image_path = r'F:\SteamLibrary\steamapps\common\Grand Theft Auto V\cv_saved\GTAV_2025-08-30_492686459_depth.npy'
camera_params_path = r'F:\SteamLibrary\steamapps\common\Grand Theft Auto V\cv_saved\GTAV_2025-08-30_492686459_meta.json'

# Load RGB and Depth images
rgb_image = np.array(Image.open(rgb_image_path))
depth_image = np.load(depth_image_path)

# Load camera parameters
with open(camera_params_path, 'r') as f:
    camera_params = json.load(f)

# Extract camera extrinsics and intrinsics
extrinsics = np.array(camera_params['extrinsic_cam2world']).reshape(3, 4)
fov_v = camera_params['fov_v_degrees']

# Assume a pinhole camera model for simplicity
focal_length = 1 / np.tan(np.radians(fov_v / 2))  # Approximate focal length
center_x = rgb_image.shape[1] / 2
center_y = rgb_image.shape[0] / 2

# Create intrinsic matrix
intrinsics = np.array([
    [focal_length, 0, center_x],
    [0, focal_length, center_y],
    [0, 0, 1]
])

# Generate point cloud from depth and RGB
def generate_point_cloud(rgb_image, depth_image, intrinsics, extrinsics):
    height, width = depth_image.shape
    points = []
    colors = []

    for v in range(height):
        for u in range(width):
            depth = depth_image[v, u]
            if depth > 0:  # valid depth
                # Convert from image coordinates to camera coordinates
                x = (u - center_x) * depth / focal_length
                y = (v - center_y) * depth / focal_length
                z = depth

                # Apply extrinsic to get world coordinates
                point_camera = np.array([x, y, z, 1])
                point_world = np.dot(extrinsics, point_camera)[:3]

                points.append(point_world)
                colors.append(rgb_image[v, u] / 255.0)  # Normalize to [0, 1]

    return np.array(points), np.array(colors)

points, colors = generate_point_cloud(rgb_image, depth_image, intrinsics, extrinsics)

# Create Open3D point cloud object
pcd = o3d.geometry.PointCloud()
pcd.points = o3d.utility.Vector3dVector(points)
pcd.colors = o3d.utility.Vector3dVector(colors)

# Visualize
o3d.visualization.draw_geometries([pcd])
