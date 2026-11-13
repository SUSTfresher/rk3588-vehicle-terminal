# 在线地图最小验证（天地图 v2）

独立于 `01_ui_shell`，仅依赖 Qt Widgets 和 Qt Network，适配当前
Qt 5.12 / linuxfb。没有改动车机主程序。

## 这一步验证什么

- 屏幕上显示真实在线地图，单指拖动，双指捏合缩放，按钮也可缩放。
- 默认陕西科技大学西安校区示例位置；没有定位模块，不代表真实车辆位置。
- DNS / HTTPS / 瓦片请求失败时界面仍能退出，终端提供错误原因。
- 不包含搜索、路线规划、模拟导航，地图提供方也还未作为最终产品方案确定。

## 上板运行

先退出正在使用 framebuffer 的车机程序。上传本目录之后执行：

```bash
cd ~/vehicle_terminal/02_map_probe
qmake map_probe.pro
make -j2 && QT_QPA_PLATFORM=linuxfb QT_QPA_FB_HIDECURSOR=1 ./map_probe
```

无需 sudo，不需要 GStreamer 或 Qt WebEngine。运行前需要配置天地图 Key，
且板子必须能访问 `https://t0.tianditu.gov.cn`。创建 Key 成功不代表网络已验证。

## 配置 Key（仅在香橙派操作）

使用天地图控制台创建的服务器端应用 Key，以普通 orangepi 用户执行：

```bash
mkdir -p ~/.config/vehicle-map-probe
chmod 700 ~/.config/vehicle-map-probe
nano ~/.config/vehicle-map-probe/tianditu.key
```

文件只放一行 Key，不写 `tk=`、引号或接口地址。在 nano 中按 Ctrl+O、回车保存，
然后 Ctrl+X 退出，再执行：

```bash
chmod 600 ~/.config/vehicle-map-probe/tianditu.key
```

无需把 Key 发到聊天或写进源码。程序固定从当前用户的上述路径读取。
无配置时不会请求地图，补好配置后可点击“重试”重新读取。
启动应看到 `Map probe v2 | provider=Tianditu`、`SSL available= true`、
`Map Key configured= true`。最后一项仅表示配置已读取，不表示服务端已认可 Key。

## 代码如何工作

1. `home()` 把陕西科技大学西安校区示例经纬度转换为 Web Mercator 世界像素坐标。
2. `visibleTiles()` 计算屏幕内需要的 256×256 地图图片编号。
3. `requestVisible()` / `fetch()` 优先安排底图，再安排中文注记，两层合计最多 4 个并发请求。
4. `paintEvent()` 先画底图，再叠加透明中文注记。触摸只改变视图中心，不执行阻塞请求。
5. 每个请求超时 12 秒；失败保留提示，用户点击重试才重新加载。

Qt 的 HTTPS 支持依赖板端 SSL 库。启动日志会打印 Qt 平台、SSL 支持状态、
编译及运行时 SSL 版本；`Tile FAILED` 打印 HTTP 状态、超时标记及 Qt 网络错误数字。
不输出完整 URL、errorString 或响应正文，避免打印其中可能包含的 Key。
不要忽略 SSL 证书错误；先查系统时间、CA 证书和运行库兼容性。

## 缓存与服务

上一版 OSM 在板端出现连接超时（curl 未建立 TCP 连接），因此改用天地图。
依据[官方地图服务说明](https://lbs.tianditu.gov.cn/server/MapService.html)，
使用 HTTPS WMTS：`vec_w` 底图和 `cva_w` 中文注记，`TILEMATRIXSET=w`。
“矢量底图”是服务图层名称，本程序收到的仍是 256×256 图片。

缩放范围 3–17，只请求当前可见区域，不整城下载或后台预取。
内存最多缓存 64 张解码图片。本版不使用磁盘缓存，避免把含 Key 的请求 URL
写入 Qt 缓存元数据。重新启动会再次下载，两层均消耗请求额度。
保留地图来源文字，服务使用及后续发布应遵循天地图平台要求。

## 验收

1. 出现陕西科技大学西安校区附近道路和中文地名，底图、地名两组计数均达到总数量。
2. 单指拖动画面并松开，可以加载新的区域；`+`、`−` 或双指捏合能改变比例。
3. “陕西科技大学”恢复示例视图；“退出”可返回终端。
4. 如果加载失败，等待约 12 秒后仍可点击退出，回传启动和失败日志。

`Tile OK` 日志含图层和编号，例如 `vec/12/3372/1551`、`cva/12/3372/1551`。

- `HTTP=0 timeout=true`：在期限内没有拿到 HTTP 状态，优先检查网络可达性。
- HTTP 401/403：检查应用 Key、授权与 IP 白名单设置，具体原因以控制台为准。
- HTTP 200 但图片无效：可能返回了服务错误正文，不能据此认定请求成功。
- HTTP 3xx：本版不跟随重定向，避免将 Key 传往未知地址。

回传启动与失败日志即可，不要发送 Key 文件。本轮仅做本地源码检查；
天地图 v2 的编译、带 Key 的实际访问及触摸效果需要在香橙派验收。
