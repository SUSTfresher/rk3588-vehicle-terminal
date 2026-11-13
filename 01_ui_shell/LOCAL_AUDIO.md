# 本地音乐第一版

本文保留第一版记录；当前唱片界面、多曲目、进度条和循环模式见 MUSIC_UI.md。

## 代码分工

- main.cpp：将多媒体页面加入现有 QStackedWidget。
- media_page.h/.cpp：文件名、状态、播放/继续、暂停、停止按钮。
- audio_player.h/.cpp：GStreamer playbin 解码，alsasink 输出，处理结束和错误。
- terminal.pro：通过 pkg-config 链接 gstreamer-1.0，注册新 QObject 类供 moc 处理。

播放器和车辆数据接收器相互独立，隐藏多媒体页不会销毁播放器。
这版仍在同一进程，GStreamer 内部执行媒体处理，Qt 定时器每50ms非阻塞读取消息总线，每轮最多32条。
没有在 GUI 线程等待流水线完成状态切换；进入 NULL 的资源释放仍由 GStreamer 同步执行，尚未做耗时实测。

## 文件与声卡

将真正的 MP3 或 WAV 放在可执行文件旁的 media/test.mp3 或 media/test.wav。
同时存在时优先 MP3。每次点击播放重新查找文件，缺失时显示错误，可补传后重试。
本轮没有选曲、进度条、网络播放和自动播放。

声卡固定为已验证的 plughw:CARD=rockchipes8388,DEV=0，初始软件音量20%。
MP3是否能解码还取决于板端安装的插件；WAV可作为基础测试。
playbin只开启音频标志，不创建视频窗口。

## 编译与验证

在板端项目目录执行 qmake terminal.pro，再执行 make -j2。
本地尚无可用 Qt/GStreamer 编译环境，此改动只完成源码检查，板端编译及以下测试待用户执行：

1. 播放有声，显示播放中。
2. 暂停后无声，再点播放从原位置继续。
3. 停止后再次播放从头开始。
4. 播放时切换车辆页，音乐继续，时钟/触摸/车辆数据正常。
5. 播放到末尾显示结束，再点播放可重播。
6. 缺失或损坏音频能报错而不退出界面，放入有效音频后可重试。

依赖缺失时检查 pkg-config --modversion gstreamer-1.0 和 gst-inspect-1.0 playbin / alsasink。
播放器不需要 gstreamer-app-1.0；后续视频 appsink 才会使用它。
