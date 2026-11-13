# RK3588 Vehicle Terminal

基于 Orange Pi 5、Qt 5.12 和 GStreamer 的 720×480 Linux 车载终端原型。
项目用于学习嵌入式 Linux 应用层、Qt Widgets、GStreamer、V4L2、网络请求和车载界面设计。

## 已实现功能

- 车辆状态页：车速、挡位、模拟车辆数据和数据超时提示
- 多媒体页：本地音乐播放、暂停、切歌、播放列表、进度拖动、封面和音量控制
- 摄像头页：OV13855 实时预览、FPS、倒车模拟、RGB 色彩补偿
- 地图页：天地图瓦片、触摸拖动、双指缩放、地点搜索和候选地点标记
- 设置页：声音、连接状态、系统信息
- 触摸交互：页面左右滑动、地图拖动和缩放

项目目前是模拟终端，没有 GPS、真实 CAN 和车辆控制外设。车辆状态由本地模拟器提供。

## 目录

| 目录 | 内容 |
| --- | --- |
| `01_ui_shell` | 主车载终端、Qt 页面、GStreamer 摄像头和本地音频 |
| `02_map_probe` | 天地图瓦片和网络连通性独立验证 |
| `03_map_search_test` | 地点搜索解析和地图交互离线测试 |
| `camera_color_comparison` | OV13855 固定 RGB 色彩补偿对比工具 |
| `screenshots` | 实机界面截图（可选） |

## 板端编译运行

目标环境：Orange Pi 5、Orange Pi Ubuntu Focal、Qt 5.12.8、GStreamer 1.16。

```bash
cd ~/vehicle_terminal/01_ui_shell
qmake terminal.pro
make -j2
QT_QPA_PLATFORM=linuxfb QT_QPA_FB_HIDECURSOR=1 ./vehicle_terminal
```

模拟器另行编译运行，详见 `01_ui_shell/REVERSE_SIMULATION.md`。

## 地图 Key

地图瓦片和地点搜索都从以下路径读取天地图 Key，Key 文件不应提交到 Git：

```text
~/.config/vehicle-map-probe/tianditu.key
```

地图功能需要网络和有效的天地图服务权限。没有 GPS 时，地点标记表示搜索结果，不表示车辆实时位置。

## 摄像头色彩补偿

当前默认使用固定补偿 `R×1.48、G×0.88、B×1.12`，用于改善 OV13855 在当前环境下的偏绿色现象。
可用下面的环境变量临时关闭：

```bash
VEHICLE_CAMERA_COLOR=0 QT_QPA_PLATFORM=linuxfb ./vehicle_terminal
```

这不是自动白平衡，换摄像头或光照后需要重新评估。

## 截图

实机截图建议放在 `screenshots/`，不要把编译产物、日志、音频文件或 Key 上传。
