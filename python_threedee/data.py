import h5py
with h5py.File(r"C:\Program Files (x86)\Steam\steamapps\common\Cyberpunk 2077\bin\x64\cv_saved\actions_2025-09-12_1857605979\depth_group_000062.h5", "r") as f:
    print("Keys:", list(f.keys()))                    # ['depth']
    depth_data = f['depth'][:]                        # shape: (N, H, W)
    print("Shape of depth array:", depth_data.shape)  # e.g., (30, 1080, 1920)
    first_frame = depth_data[0]                       # 第一帧 depth 图像
