# 摄像头页面第一版

## 已验证的采集链路

板端 `/dev/video-camera0` 能通过 `v4l2-ctl` 以 NV12 720×480 约30 FPS采集。
GStreamer 直接把多平面 NV12 送到 fakesink 会遇到缓冲池分配失败，因此页面使用：

```text
v4l2src(io-mode=mmap)
  → NV12 720×480 30/1
  → videoconvert
  → RGB 720×480
  → appsink(max-buffers=1, drop=true)
```

每30ms非阻塞取一帧，appsink只保留最新帧；帧复制为QImage后交给页面显示。
当前转换会使用CPU，后续根据实测CPU占用再评估RGA/DMABUF零拷贝。

## 页面布局

当前采用方案A：实时影像占据页面主体，顶部和底部使用半透明叠层显示标题、状态、FPS及开始/停止预览。
进入摄像头页后隐藏全局底部导航，顶部“返回”按钮负责回到车辆状态页；离开页面会停止采集，进入页面自动启动。

模拟器现在会在 P→D 行驶→减速→R→P 的循环中发送 R 挡。
收到 R 挡时主界面自动进入此页面，退出 R 挡后自动回到车辆状态页；手动点击摄像头页时仍可独立预览。

## 代码分工

- camera_capture.h/.cpp：GStreamer流水线、总线错误、最新帧、FPS统计。
- camera_page.h/.cpp：实时画面、状态、FPS、开始/停止按钮。
- main.cpp：第三个页面接入；进入页面自动启动，离开页面自动停止。
- terminal.pro：加入 gstreamer-app-1.0 和新源文件。

## 板端验收

重新 qmake 和 make 后，启动界面并进入“摄像头”：

1. 画面出现，状态显示摄像头已启动，FPS接近30。
2. 点击停止，画面清空、FPS回到0；点击开始后恢复。
3. 切到车辆状态或多媒体，采集停止；再次进入摄像头后恢复。
4. 拔出/占用摄像头时显示错误，界面不退出。
5. 播放音乐时进入摄像头，确认视频采集不会让车辆数据和触摸卡住。

本页面暂不做倒车自动切换、录像、拍照和硬件零拷贝。

画面亮度暂未在程序中强制修改。先读取摄像头曝光、增益、亮度控制项，避免用软件放大亮度掩盖传感器曝光问题。

## OV13855 色彩补偿

默认对每帧 RGB888 图像应用 R×1.48、G×0.88、B×1.12，采用已确认的
`camera_color_comparison/refined.png` 预览参数。实现位于 `camera_color.h`，
由 `camera_capture.cpp` 在复制采集帧并释放 GStreamer 映射后调用。
每帧只处理一次，使用三张 256 项查找表并限制输出到 0～255。

这是室内场景的固定色彩补偿，不是自动白平衡，也不调整曝光、增益、方向或暗角。
光照变化后可先关闭补偿对比。环境变量在程序启动时读取：

```bash
VEHICLE_CAMERA_COLOR=0 QT_QPA_PLATFORM=linuxfb QT_QPA_FB_HIDECURSOR=1 ./vehicle_terminal
```

不设置 `VEHICLE_CAMERA_COLOR` 时默认开启。每次启动采集会打印
`Camera color correction: ON (R=1.48 G=0.88 B=1.12)` 或 `OFF`。
板端验收需检查颜色、预览流畅度，以及重复进入/退出和 R 挡自动切换。

本地 C++ 校正测试与已确认 PNG 逐通道比较无差异，同时检查了原图保留、
RGB 行末填充和高亮截断。该测试使用桌面 Qt 6，不代替板端 Qt 5.12／GStreamer 联调。
