# Mconnect for SailfishOS

面向 **SailfishOS 5.1 / aarch64** 的 Flutter 音乐应用，基于 [Mconnect-Music_connect](https://github.com/Hjdd14/Mconnect-Music_connect) 1.5.0 改写。由 Sailfish Wayland embedder 直接运行，
发布格式是 RPM。需要相同版本的 `flutter-sfos-3.44.0` 运行时。

## 功能

保留原应用的网易云、QQ、酷狗适配器，并新增哔哩哔哩音乐来源。支持歌曲/歌单搜索、推荐、排行榜、
专辑与歌手、登录会话、歌单导入和管理、收藏、历史、队列、同步歌词、
本地扫描、下载与离线缓存、备份恢复、听歌统计、智能歌单、定时停止和淡入淡出。
这些是已移植的代码范围，平台接口、账号与实际播放仍需逐项真机验证。

Sailfish 实现位于 `lib/sailfish/`：

- 音频通过随 RPM 安装的 `libmconnect_audio.so` 调用系统 GStreamer playbin，
  使用 Sailfish `x-maemo` 系统媒体音量组；应用增益在独立软件音量节点处理，
  不覆盖系统音量。支持播放、暂停、定位、音量、变速、均衡器。
- MPRIS 提供系统媒体元数据和播放/暂停/上一首/下一首/定位。
- 文件选择在应用中完成，可选择音乐目录、SD 卡、图片和 JSON 备份。
- XDG 应用数据与缓存目录，SQLite 和 Hive 本地持久化。
- 显式使用 Wayland EGL；采用系统 Sail Sans Pro Light 主字体，文泉驿正黑与 Amiri 回退，
  页面标题使用常规字重。字体引用手机现有文件，不把中文字体替代到英文和数字上。
- 音乐库与发现的子页面保留底部导航，播放小窗始终位于导航上方；点击当前标签返回其首页。
- 登录凭据采用权限为 0700 的目录和 0600 文件，**未做静态加密，也不是系统密钥库**。
- 分享采用应用内复制/打开文件；浏览器与文件管理器通过 `xdg-open` 打开。
- Wi-Fi 下载策略检查内核默认路由；未知连接不视为 Wi-Fi。

Android 悬浮歌词、桌面小组件、Android Auto、Android 后台服务和均衡器通道
不适用于 Sailfish。外部 `mconnect://` 链接唤起尚未接入，歌单链接可以在应用内粘贴导入。
系统来电抢占、锁屏长时间播放、蓝牙路由、不同编码和登录接口必须以真机结果为准。

## 构建 RPM

需要 Linux/WSL、Docker、QEMU aarch64 与 `sfosbuild`。
从上游 `flutter-sailfishos` 3.44.0-7 发布页取得 runtime 和 devel RPM，放入：

```text
../../versions/3.44.0/runtime/rpms/
```

在本应用目录执行：

```sh
chmod +x elinux/build.sh
./elinux/build.sh
```

或设置 `FLUTTER_SFOS_RPMS=/绝对路径`。构建驱动会使用同版本的 AOT 工具，
RPM 依赖 `flutter-sfos-3.44.0`，不会把多个 Flutter 引擎塞进应用包。
不能混用其他版本 SDK 生成的 `libapp.so`。

安装 runtime 和应用 RPM 后，以普通 Sailfish 用户启动 `harbour-mconnect`。
这是需要额外运行时、关闭 Sailjail 的第三方发行应用，不能直接提交 Harbour。
当前未提供 armv7hl 包。

## 验证

```sh
flutter pub get
flutter analyze --no-pub
flutter test --no-pub -j 1
```

测试连接本地 flutter_tester WebSocket，代理环境需将 localhost/127.0.0.1 排除。
构建与验证结果见 `VALIDATION.md`。本地状态位于
`~/.local/share/harbour-mconnect`，缓存位于 `~/.cache/harbour-mconnect`，
或相应的 XDG 环境变量目录。卸载 RPM 不会主动删除用户音乐或应用数据。

上游许可证保留于 `LICENSE`。

播放已有下载或缓存的歌曲时始终优先读取本地文件，无需打开“离线模式”；只有找不到有效文件时才尝试网络取流。首次播放会等待下载记录恢复完成。

正在播放页的歌词区在竖屏下占满标题栏与控制区之间的剩余空间；发现页推荐卡片与页面统一滚动，首页最多展示 9 首。短屏与大字模式保留滚动回退。

应用 cover 仅显示封面、最多两行歌名和播放/暂停、下一曲按钮；歌名根据其
背景图区域的亮度选择深浅字色。按钮连接现有 MPRIS 播放会话。
原生 cover 使用独立共享内存 surface，Qt 不创建主界面窗口或接管 Flutter 输入。
原生按钮/状态回归可通过 `MCONNECT_BUILD_COVER_TESTS=ON` 构建
`mconnect_cover_test`，在设备上以 `dbus-run-session -- ./mconnect_cover_test` 运行；
测试使用隔离的模拟播放服务，不会播放音乐。

哔哩哔哩音乐基于 [BiliMusic](https://github.com/eason919191/BiliMusic) 的 Web API 适配；支持范围见 [docs/bilibili.md](docs/bilibili.md)。

当前 RPM 版本为 `1.5.0-13`。关闭应用会结束播放进程；普通切后台保留播放和 cover。窗口生命周期协议测试可用 `MCONNECT_BUILD_COVER_TESTS=ON` 构建 `mconnect_window_lifecycle_test`，无需真实音频设备。

构建运行器来自 [flutter-sailfishos](https://github.com/MrCyjaneK/flutter-sailfishos)。独立检出本仓库时，下载匹配版本的 runtime/devel RPM 到同一目录，并设置 `FLUTTER_SFOS_RPMS` 指向它。
