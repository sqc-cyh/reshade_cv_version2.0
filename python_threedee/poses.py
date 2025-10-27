import open3d as o3d
import numpy as np
import glob, os
import json

# hyperparameter
scale_factor = 100 # for better visualization
N_global = 100 # number of points for global XYZ
N_camera = 20 # number of points for camera xyz

# visualize the global XYZ
global_x = np.zeros((N_global, 3))
global_x[:,0] = np.linspace(0, 1, N_global)
global_x_color = np.zeros(global_x.shape)
global_x_color[:,0] = 1

global_y = np.zeros((N_global, 3))
global_y[:,1] = np.linspace(0, 1, N_global)
global_y_color = np.zeros(global_y.shape)
global_y_color[:,1] = 1

global_z = np.zeros((N_global, 3))
global_z[:,2] = np.linspace(0, 1, N_global)
global_z_color = np.zeros(global_z.shape)
global_z_color[:,2] = 1

# load camera's RT matrix
c2ws = []
json_paths = sorted(glob.glob('*.json'))
for json_path in json_paths:
    with open(json_path) as f:
        c2w = np.array(json.load(f)['extrinsic_cam2world']).reshape(3, 4)
        c2ws.append(c2w)
c2ws = np.array(c2ws)
print(c2ws.shape)
# c2ws[:, :3, 3] /= 100
# visualize the camera xyz
camera_centers = c2ws[:,:3,3] / scale_factor
camera_centers_color = np.zeros(camera_centers.shape)

camera_xs = np.linspace(0, 0.1, N_camera).reshape(N_camera, 1, 1)
camera_x_dirs = c2ws[:,:3,0]
camera_x_dirs = camera_x_dirs.reshape(1, *camera_x_dirs.shape)
camera_xs = camera_xs * camera_x_dirs + camera_centers[None]
camera_xs = camera_xs.reshape(-1, 3)
camera_xs_color = np.zeros(camera_xs.shape)
camera_xs_color[:,0] = 1

camera_ys = np.linspace(0, 0.1, N_camera).reshape(N_camera, 1, 1)
camera_y_dirs = c2ws[:,:3,1]
camera_y_dirs = camera_y_dirs.reshape(1, *camera_y_dirs.shape)
camera_ys = camera_ys * camera_y_dirs + camera_centers[None]
camera_ys = camera_ys.reshape(-1, 3)
camera_ys_color = np.zeros(camera_ys.shape)
camera_ys_color[:,1] = 1

camera_zs = np.linspace(0, 0.1, N_camera).reshape(N_camera, 1, 1)
camera_z_dirs = c2ws[:,:3,2]
camera_z_dirs = camera_z_dirs.reshape(1, *camera_z_dirs.shape)
camera_zs = camera_zs * camera_z_dirs + camera_centers[None]
camera_zs = camera_zs.reshape(-1, 3)
camera_zs_color = np.zeros(camera_zs.shape)
camera_zs_color[:,2] = 1

# plots
pts = np.concatenate([
    # camera_centers,
    # global_x, global_y, global_z, 
    camera_xs, camera_ys, 
    camera_zs,
], axis=0)
colors = np.concatenate([
    # camera_centers_color,
    # global_x_color, global_y_color, global_z_color, 
    camera_xs_color, camera_ys_color, 
    camera_zs_color,
], axis=0)

pcd = o3d.geometry.PointCloud()
pcd.points = o3d.utility.Vector3dVector(pts.reshape(-1,3))
pcd.colors = o3d.utility.Vector3dVector(colors.reshape(-1,3))

app = o3d.visualization.gui.Application.instance
app.initialize()

vis = o3d.visualization.O3DVisualizer()
vis.add_geometry("Points", pcd)

for idx in range(0, len(camera_centers)):
    vis.add_3d_label(pcd.points[idx], "{}".format(idx+1))
vis.reset_camera_to_default()

app.add_window(vis)
app.run()










