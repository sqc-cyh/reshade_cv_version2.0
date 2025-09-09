# Render Image

这个示例演示了如何生成相机poses并渲染一系列图像。该示例现在采用两阶段工作流程：

## 使用方法

```console
# 第一步：生成相机poses
python generate_camera_poses.py

# 第二步：渲染图像
python run.py
```

## 工作流程说明

### 第一阶段：生成相机poses
运行 `generate_camera_poses.py` 将：
- 从当前玩家相机位置收集多个相机poses
- 保存相机位置、旋转、FOV和视口信息
- 将所有poses保存到 `camera_poses/camera_poses.csv` 文件中

你可以在游戏中移动相机到不同位置，脚本会自动采集这些位置作为渲染poses。默认收集50个poses，可以通过修改脚本中的 `num_camera_poses` 变量来调整。

### 第二阶段：渲染图像
运行 `run.py` 将：
- 读取之前生成的CSV文件中的相机poses
- 为每个pose渲染RGB图像和深度图像
- 保存渲染结果到 `output/` 目录，包括：
  - RGB图像 (PNG格式)
  - 深度数据 (NPY格式)
  - 深度可视化图像 (PNG格式)
  - 相机pose信息 (JSON和NPZ格式)
  - 元数据文件 (TXT格式)

## 输出文件

每个渲染的pose会生成以下文件：
- `render_0000_rgb_TIMESTAMP.png` - RGB图像
- `render_0000_depth_TIMESTAMP.npy` - 深度数据
- `render_0000_depth_viz_TIMESTAMP.png` - 深度可视化
- `render_0000_pose_TIMESTAMP.json` - 相机pose (JSON格式)
- `render_0000_pose_TIMESTAMP.npy` - 相机pose (NumPy格式)
- `render_0000_metadata_TIMESTAMP.txt` - 元数据信息

文件名中的数字对应CSV文件中的pose索引。

## 配置

在运行之前，请：
1. 将 `user_config.yaml.example` 重命名为 `user_config.yaml`
2. 修改配置文件中的路径以适配你的系统

## 优势

这种两阶段方法的优势：
- **可重复性**: 可以使用相同的相机poses多次渲染
- **调试友好**: 可以分别调试pose生成和渲染过程
- **批处理**: 可以一次性渲染大量预定义的poses
- **数据一致性**: 所有渲染使用完全相同的相机参数