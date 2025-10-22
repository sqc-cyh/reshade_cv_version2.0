#!/usr/bin/env python3
import os
import cv2
import h5py
import numpy as np
from PIL import Image
import argparse
from tqdm import tqdm

def unpack_data_to_frames(data_dir, output_dir=None):
    """
    将 Cyberpunk 2077 的 capture.mp4 + depth_group_*.h5 + camera.json
    解包为每帧独立的:
        frame_XXXXXX_depth.npy
        frame_XXXXXX_RGB.png
        frame_XXXXXX_camera.json (已存在)
    """
    if output_dir is None:
        output_dir = data_dir

    video_path = os.path.join(data_dir, "capture.mp4")
    # depth_dir = data_dir
    cam_dir = data_dir
    rgb_output_dir = output_dir
    # depth_output_dir = output_dir

    os.makedirs(rgb_output_dir, exist_ok=True)
    # os.makedirs(depth_output_dir, exist_ok=True)

    # print("🔍 扫描 depth_group_*.h5 文件...")
    # depth_files = sorted(f for f in os.listdir(depth_dir) if f.startswith("depth_group_") and f.endswith(".h5"))
    # assert len(depth_files) > 0, f"未找到 depth_group_*.h5 文件于 {depth_dir}"

    # # === Step 1: 从 .h5 提取 depth 帧 ===
    # global_frame_idx = 0
    # print("📦 解包 depth 数据...")
    # for h5_file in tqdm(depth_files, desc="Processing depth chunks"):
    #     h5_path = os.path.join(depth_dir, h5_file)
    #     with h5py.File(h5_path, 'r') as hf:
    #         assert 'depth' in hf, f"'depth' dataset not found in {h5_file}"
    #         depth_stack = hf['depth'][:]  # shape: (N, H, W)

    #         for i in range(depth_stack.shape[0]):
    #             frame_id = global_frame_idx + i
    #             depth_data = depth_stack[i]  # (H, W)

    #             # 保存为 .npy
    #             depth_filename = os.path.join(depth_output_dir, f"frame_{frame_id:06d}_depth.npy")
    #             np.save(depth_filename, depth_data.astype(np.float32))

    #     global_frame_idx += depth_stack.shape[0]

    # total_frames = global_frame_idx
    # print(f"✅ 共提取 {total_frames} 帧 depth 数据.")

    # === Step 2: 从视频提取 RGB 图像 ===
    print("🎥 解包 RGB 视频帧...")
    cap = cv2.VideoCapture(video_path)
    assert cap.isOpened(), f"无法打开视频文件: {video_path}"

    total_video_frames = int(cap.get(cv2.CAP_PROP_FRAME_COUNT))
    # assert total_video_frames >= total_frames, \
    #     f"视频帧数 ({total_video_frames}) 少于 depth 帧数 ({total_frames})"

    for frame_idx in tqdm(range(total_video_frames), desc="Extracting RGB frames"):
        ret, bgr = cap.read()
        if not ret:
            print(f"⚠️ 无法读取第 {frame_idx} 帧")
            break

        rgb = cv2.cvtColor(bgr, cv2.COLOR_BGR2RGB)
        img = Image.fromarray(rgb)

        # 保存为 PNG
        png_filename = os.path.join(rgb_output_dir, f"frame_{frame_idx:06d}_RGB.png")
        img.save(png_filename, format='PNG')

    cap.release()
    print(f"✅ 成功提取 {total_video_frames} 帧 RGB 图像.")

    # === Step 3: 检查 camera.json 是否齐全（可选）===
    missing_cams = []
    print("📄 验证 camera.json 文件...")
    for i in range(total_video_frames):
        cam_file = os.path.join(cam_dir, f"frame_{i:06d}_camera.json")
        if not os.path.isfile(cam_file):
            missing_cams.append(i)

    if missing_cams:
        print(f"❌ 缺失 {len(missing_cams)} 个 camera.json: 示例 {missing_cams[:5]}...")
    else:
        print("✅ 所有 camera.json 存在.")

    print(f"🎉 解包完成！输出目录: {output_dir}")
    print(f"📊 总帧数: {total_video_frames}")
    print("📁 输出文件示例:")
    print(f"   {os.path.join(output_dir, 'frame_000000_depth.npy')}")
    print(f"   {os.path.join(output_dir, 'frame_000000_RGB.png')}")
    print(f"   {os.path.join(output_dir, 'frame_000000_camera.json')}")


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description="将 Cyberpunk 2077 的 h5+mp4 数据解包为每帧独立文件")
    parser.add_argument("data_dir", help="包含 capture.mp4 和 depth_group_*.h5 的目录")
    parser.add_argument("--output_dir", type=str, default=None, help="输出目录（默认为 data_dir）")
    args = parser.parse_args()

    unpack_data_to_frames(args.data_dir, args.output_dir)
