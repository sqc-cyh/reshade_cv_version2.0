
import argparse, glob, json, math, os
import numpy as np
import open3d as o3d

def build_intrinsics_from_vfov(H, W, fov_v_deg):
    fy = (H/2.0) / math.tan(math.radians(fov_v_deg)/2.0)
    fx = fy * (W/float(H))
    cx, cy = W/2.0, H/2.0
    K = np.array([[fx, 0,  cx],
                  [0,  fy, cy],
                  [0,  0,   1]], dtype=np.float64)
    return K

def frustum_lineset(K, H, W, near=0.1, far=5.0, color=(1.0, 0.2, 0.2)):
    fx, fy, cx, cy = K[0,0], K[1,1], K[0,2], K[1,2]
    corners = np.array([[0,0], [W,0], [W,H], [0,H]], dtype=np.float64)
    def unproject(depth):
        zs = np.full((4,1), depth, dtype=np.float64)
        xs = (corners[:,0:1]-cx)/fx * zs
        ys = (corners[:,1:2]-cy)/fy * zs
        return np.hstack([xs, ys, zs]) # (4,3)
    near_pts = unproject(near)
    far_pts  = unproject(far)
    origin = np.zeros((1,3), dtype=np.float64)
    V = np.vstack([origin, near_pts, far_pts])
    E = [
        [0,1],[0,2],[0,3],[0,4],
        [1,2],[2,3],[3,4],[4,1],
        [5,6],[6,7],[7,8],[8,5],
        [1,5],[2,6],[3,7],[4,8]
    ]
    C = np.tile(np.array(color, dtype=np.float64), (len(E),1))
    ls = o3d.geometry.LineSet()
    ls.points = o3d.utility.Vector3dVector(V)
    ls.lines  = o3d.utility.Vector2iVector(np.array(E, dtype=np.int32))
    ls.colors = o3d.utility.Vector3dVector(C)
    return ls

def to_transform(mat3x4):
    T = np.eye(4, dtype=np.float64)
    T[:3,:3] = mat3x4[:,:3]
    T[:3, 3] = mat3x4[:, 3]
    return T

def load_cam2world_from_meta(meta_path):
    with open(meta_path, "r") as f:
        m = json.load(f)
    A = np.array(m["extrinsic_cam2world"], dtype=np.float64).reshape(3,4)
    Tcw = to_transform(A)
    vfov = float(m["fov_v_degrees"])
    return Tcw, vfov

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--files", nargs="*", default=[], help="List of *_meta.json files")
    ap.add_argument("--image_size", type=int, nargs=2, default=[1080,1920], help="H W for the images (default 1080 1920)")
    ap.add_argument("--near", type=float, default=0.1)
    ap.add_argument("--far", type=float, default=8.0)
    ap.add_argument("--world_to_cam", action="store_true", help="If your matrices are world->cam, set this to avoid inversion")
    args = ap.parse_args()

    if not args.files:
        args.files = sorted(glob.glob("*_meta.json"))

    if not args.files:
        print("No meta files provided/found.")
        return

    H, W = args.image_size
    geoms = []
    colors = [
        (1.0,0.2,0.2),(0.2,1.0,0.2),(0.2,0.4,1.0),
        (1.0,0.7,0.2),(0.8,0.2,1.0),(0.2,1.0,1.0)
    ]

    for idx, f in enumerate(args.files):
        Tcw, vfov = load_cam2world_from_meta(f)  # cam->world by default in this dataset
        K = build_intrinsics_from_vfov(H, W, vfov)
        color = colors[idx % len(colors)]
        fr = frustum_lineset(K, H, W, near=args.near, far=args.far, color=color)
        if args.world_to_cam:
            Twc = np.eye(4); Twc[:3,:3] = np.linalg.inv(Tcw[:3,:3]); Twc[:3,3] = -Twc[:3,:3] @ Tcw[:3,3]
        else:
            Twc = Tcw
        fr.transform(Twc)
        geoms.append(fr)
        axis = o3d.geometry.TriangleMesh.create_coordinate_frame(size=0.5)
        axis.transform(Twc)
        geoms.append(axis)
        print(f"Added {os.path.basename(f)}  vFOV={vfov:.3f}  pose=\n{Twc}")

    o3d.visualization.draw_geometries(geoms)

if __name__ == "__main__":
    main()
