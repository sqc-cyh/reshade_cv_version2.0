import glob
import os
import numpy as np
import sys

def main():
    if len(sys.argv) != 2:
        print("用法: python flip_depth_inplace.py <文件夹>")
        sys.exit(1)

    input_dir = sys.argv[1]

    if not os.path.exists(input_dir):
        print(f"错误：输入文件夹不存在 -> {input_dir}")
        sys.exit(1)

    # 匹配所有 .npy 文件
    file_list = glob.glob(os.path.join(input_dir, "*.npy"))
    if not file_list:
        print(f"未找到任何 .npy 文件在 {input_dir}")
        sys.exit(0)

    print("匹配到的文件：", file_list)

    for file_path in file_list:
        depth = np.load(file_path)
        depth_fixed = np.flipud(depth)
        # 直接覆盖原文件
        np.save(file_path, depth_fixed)
        print(f"已覆盖: {os.path.basename(file_path)}")

if __name__ == "__main__":
    main()
