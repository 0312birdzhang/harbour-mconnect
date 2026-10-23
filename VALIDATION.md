# 验证记录

日期：2026-10-20。设备与主机结果分开记录，不把原生库测试当作完整应用验收。

## 已确认

- 主机 Flutter 3.44.0 / Dart 3.12.0，应用依赖解析成功。
- `flutter analyze --no-pub`：0 issues。
- 最终默认测试全集：1536 passed，11 skipped，0 failed。
- 新增 MPRIS 测试使用独立、真实的 D-Bus 服务，验证元数据、播放、暂停、
  下一首、相对定位和拒绝过期 track ID。
- 凭据测试验证跨实例读取、并发写入、原子替换、删除，以及 0700/0600 权限。
- 外部文件调用测试验证文件名直接作为 argv 传递。
- 音频 FFI 测试验证缓冲期间保留播放意图、EOS、错误上报，以及加载中释放
  不会再次访问已销毁的句柄。
- 原生音频桥接库通过 Sailfish SDK aarch64 编译（`-Wall -Wextra -Werror`）。

## 真实设备

- 系统：SailfishOS 5.2.0.20 / aarch64。
- GStreamer 1.26.11、SQLite 3.50.4、PulseAudio 与 maliit-glib 已存在。
- 新装 `flutter-sfos-3.44.0-3.44.0-7.aarch64`，原有
  `flutter-sfos-3.41.9-3.41.9-13.aarch64` 保持安装。
- 上传原生桥接库，通过 `tools/device-audio-smoke.py` 的真实 GStreamer
  `fakesink` 静音测试：3 秒 WAV 解码、播放/暂停、定位、1.25 倍速、均衡器、
  EOS、EOS 后重播、无效文件错误上报均通过。
- 测试库 SHA-256：
  `c85d8b174f3f7479a26a1eaa6984245bf93439ed259a1cb5e782c895e27bee04`。

## 完整应用状态

完整 ARM64 AOT、Wayland runner、GStreamer 桥接和 RPM 构建通过。
实际 RPM 内容校验通过：包含三份 aarch64 ELF、Flutter 资源、图标、启动器，
依赖匹配的 3.44.0 引擎，不内嵌另一份引擎；已移除 GTK 图标缓存工具依赖。

真机首屏、中文显示、点击与 Sailfish 输入法通过截图验证。
通过输入法搜索 `hello`，网易云结果与封面正常返回；实际播放 OMFG 的
`Hello`，时长约 226 秒，GStreamer 解码并创建 PulseAudio `media.role=music`
音频流。MPRIS 元数据、暂停、10 秒相对跳转、继续及播放位置变化均验证通过。
用户确认手机有声输出，并因音量较大手动暂停；之后保持暂停，不再进行有声测试。

修复过的真机问题：默认 EGL 平台导致原生窗口断言，启动器显式设置
`EGL_PLATFORM=wayland`；中文字体缺字，通过字体资源清单引用系统文泉驿字体。

最终 RPM 已重新安装，以普通用户正常启动；`rpm -V harbour-mconnect` 无差异，
MPRIS 状态为 `Paused`，截图显示播放记忆已恢复但没有自动播放。
RPM SHA-256：`09c1c471c276ecea7bf4dae4637c05e056d75150ae0a5bdd3cde82fc2a2476a6`。
真机截图：最终首屏（设备图片不公开）、搜索与输入法（设备图片不公开）。

后续还需验证：QQ/酷狗搜索与播放、各平台登录、同步歌词、锁屏长时间播放、
来电抢占、蓝牙/耳机路由及下载后的离线播放。账号与会员限制按平台响应处理；
此次网易云测试遇到会员歌曲时，应用显示相应错误并跳过。

`upstream_platform_tests/` 保留但不计入默认测试集的 Android/Windows 专属测试。

## 1.5.0-2 标题中文修复

补充 Ubuntu、Arial、sans-serif 字体资源别名，覆盖没有指定字体的标题样式。
此前仅注册 Roboto，正文正常但 AppBar 等独立样式仍会缺字。
此次未更改 Dart/AOT 和音频逻辑，重新生成字体清单并打包。

真机截图确认：下载管理（设备图片不公开）、
音乐库「我喜欢」（设备图片不公开）、
发现「榜单中心」（设备图片不公开）标题中文正常。
最终 1.5.0-2 RPM 资源验证通过，已安装并重新启动；`rpm -V` 无差异，
MPRIS 状态保持 `Paused`，全程未触发有声播放。

新 RPM SHA-256：`4366e1f8097f5f65321dd17b5ccf6af954c93a4ad4731c843e3b19a74935b504`。

## 1.5.0-3 系统音乐音量修复

真实故障流的 `media.role=music`、restore ID 为 `sink-input-by-media-role:music`，
其音量保持 100%，没有跟随设备 `/etc/pulse/x-maemo-stream-restore.table` 中的
`x-maemo` 媒体音量。现改用 `media.role=x-maemo`。同时增加独立 GStreamer
`volume` 节点，应用音量与淡入淡出只调节 PCM 软件增益，不再调用 playbin 的
音量属性，避免 GstStreamVolume 改写 PulseAudio 系统流音量。没有修改手机
PulseAudio 配置或重启系统音频服务。

原生库通过 SDK aarch64 `-Wall -Wextra -Werror` 编译，静音 fakesink 回归通过
播放/暂停、定位、倍速、均衡器、EOS 与重播、错误上报。

`tools/device-volume-smoke.py` 对**最终安装 RPM 内的库**进行真实 pulsesink 测试，
输入只含全零 PCM，不播放音乐或测试音。系统媒体档位 0 → 2 → 0 时，流音量
分别为 0 → 19289 → 0；每档下应用增益依次 0、0.1、0.5、1.0，均未改变系统
流音量，role/restore ID 为 `x-maemo`。测试通过并恢复原系统档位。

1.5.0-3 已安装并正常启动，最终 RPM 内容校验通过；未进行有声播放验收。
RPM SHA-256：`c976d135a8354c2dee73364e3744635c2228a4e1ecabba3f088a2d91b7aac3e6`。

## 1.5.0-4 子页面底部导航修复

Shell 内的普通子页面现在保留底部导航，迷你播放器仍由 Shell 统一绘制，
不会出现隐藏导航后留下的悬空间距。子页面点击当前标签可以返回该标签首页；
全屏播放器与队列仍使用独立路由。

导航、下载页和播放器布局定向测试：49 passed；静态分析：0 issues。
ARM64 AOT/RPM 构建与包内容验证通过。真机确认「我喜欢」「听歌历史」
「榜单中心」保留导航，迷你播放器在导航上方，点击当前「音乐库」返回首页。
已安装 1.5.0-4，`rpm -V harbour-mconnect` 无差异，状态保持 Paused。
最终包内音频库的全零 PCM 系统音量 0 → 2 → 0 回归仍通过，全程未播放音乐。

RPM SHA-256：`d4df231379ceca9a0a341e66c3a2ebfc89ce3506cb500b6274f6c10674868678`。

## 1.5.0-5 系统字体组合

主字体改为 Sail Sans Pro Light，中文回退使用独立的 SailfishCJK 别名
（文泉驿正黑），阿拉伯文回退使用 SailfishArabic（Amiri）。字体资源均以
符号链接引用系统字体文件，并在 RPM 中声明文件依赖。旧版本把整套文字
映射到文泉驿正黑的做法已移除。明暗主题均设置主字体与回退列表，AppBar
独立标题样式也显式设置字体组合，字重从 w600 调整为 w400。

36 项主题、播放器布局和导航回归通过；静态检查 0 issues。
ARM64 AOT/RPM 构建及包内字体映射、路径、依赖校验通过。
1.5.0-5 已安装，真机截图确认下载页、我喜欢、搜索页中文正常，数字及 QQ
字母使用纤细的 Sail Sans Pro Light，未重新出现中文方框。未触发音乐播放。
`rpm -V harbour-mconnect` 无差异，MPRIS 状态保持 Paused。
RPM SHA-256：`fd3d417da390eddb58d310f18db7e44d23c7a24a9e50637709819850b7f1934d`。

## 1.5.0-6 下载文件播放优先级

修复“音乐库 → 歌单 → 我的歌单 → 播放全部”默认直接联网取流的问题。
播放器不再以离线模式开关作为查找本地文件的前提，歌单首曲、下一首以及
恢复播放均优先查询已有下载/缓存。下载记录懒加载时，路径查询等待 ready，
避免首次播放在记录恢复前误判为未下载。路径匹配仍要求平台与歌曲 ID 相同、
任务完成且文件存在，保留手动下载优先和缺失文件回退网络的行为。

118 项播放器、下载策略、歌单交互回归通过；静态分析 0 issues。
新增默认在线设置下歌单首曲/下一首、恢复播放及延迟加载下载记录的回归。

ARM64 AOT/RPM 构建及 37 文件/3 ARM64 ELF/系统字体依赖校验通过。
真机安装 1.5.0-6，rpm -V 无差异。在独立 network namespace（无路由）中
通过实际界面进入“音乐库 → 歌单 → 我的歌单 → 详情 → 播放全部”，日志确认
get url=file:///home/defaultuser/Music/netease/mp3/...，setUrl 完成并启动。
初始 PULSE_SINK 虚拟输出设置被 Sailfish 音频策略覆盖，发现后立即暂停，
显式将该流移动到 module-null-sink 后继续检查：每 200ms 验证输出 sink，
Position 从 14072542 增至 19038502 微秒，状态 Playing，最后 Pause。
因此不将此次验证描述为全程静音。移除临时虚拟输出并正常重启应用，保留暂停。
测试截图与日志仅保存在忽略的 .tools，未将个人歌单写入发布资源。
RPM SHA-256：`48e39ba0f2cdb86686d47bcc2641e568a098278ba07f77d0b844e0238efbffeb`。

## 1.5.0-7 歌词与发现页布局

歌词模式移除 400dp 高度上限和整体居中留白，由有界 Column 为歌词分配
控制区自然布局后的剩余高度，顶部距 AppBar 8dp。短屏/大字保持可滚动回退，
封面模式维持原布局。歌词列表内部上下留白从 80dp 缩至 16dp。
发现页移除固定 220/300/360dp 推荐视口，推荐网格 shrinkWrap 并禁用独立滚动，
与标题/入口共用页面滚动；每个平台取前 9 首，首页总量仍最多 9 首。

47 项播放器页面、歌词、迷你播放器和发现页测试通过，静态分析 0 issues。
布局断言覆盖歌词紧接 AppBar、840dp 高屏歌词超过 400dp、控制按钮可见、
发现页推荐区间距及第九张卡片能够随整页滚动到可点击位置。

ARM64 AOT/RPM 构建及包内容校验通过，已安装 1.5.0-7，rpm -V 无差异。
1032×2272 真机截图确认：歌词框上缘从约 y=549 移到 y=211，紧接标题栏，
底缘约 y=1678，控制按钮完整可见；发现页完整显示三排 9 首推荐及标题/歌手，
不再在第二排截断或留下原来的大段空白。此次仅查看页面，未触发播放；
最终 MPRIS Paused，运行日志未发现 RenderFlex overflow。
截图保留在忽略的 .tools/layout7-after-discovery.png 和 layout7-after-lyrics2.png。
RPM SHA-256：`ead839302a5362a55f5ae9d482f38dca5986813aeaf8da7433033a9a56f75bb6`。

## 1.5.0-10 原生 cover

最终 cover 仅显示专辑封面背景、最多两行歌名及播放/暂停、下一曲原生按钮。
歌名字色取样实际文字区域背景的线性相对亮度，选择深色或白色文字，并加入
对应浅/深遮罩；按钮区保留暗色底保证原生图标可读。空歌曲隐藏控制按钮。

弃用 Qt 与 Flutter 共用输入处理的试验：该试验曾在 QtWayland 键盘焦点转换
中崩溃。最终版本使用宿主 Wayland 连接上的独立 wl_shm cover surface，Qt
仅在自己的连接上提供字体/图像服务，不创建 Qt 窗口，也不处理 Flutter 输入。
cover 读取同一 MPRIS 播放会话，原生按钮发布 _APP_COVER_ACTION，并在按
服务名生成的完整对象路径上注册 org.sailfishos.silica.coveraction.trigger(int)。
修复试验版按钮使用错误对象路径和接口名、点击无响应的问题。

SDK ARM64 原生编译通过。在设备独立 dbus-run-session 中运行 native_cover_test，
5 组检查通过：按文字区域选择字色；暂停状态和本地封面；真实 D-Bus action
播放/暂停及 playing 状态更新；下一曲及歌名更新；空/不可用播放器拒绝操作。
测试使用模拟 MPRIS，不触及正常会话的播放器和音频设备。

RPM 38 文件/3 ARM64 ELF/Qt 与 runtime 依赖校验通过。此次只重建原生宿主，
沿用 1.5.0-9 的 Dart AOT 与音频库。已安装启动 1.5.0-10，rpm -V 无差异，
运行进程及完整 cover 控制接口可访问；无效 action ID 返回且不会发起播放。
按用户要求停止自行截图，最终颜色/排版与实际触摸效果由用户反馈验收。
SHA-256：38d87b409a83dfc2088b3e1f211450e86677159f1c1941367a1bccb15c6e08d8。

## 1.5.0-11 至 1.5.0-13

哔哩哔哩搜索、音频地址与 CDN 数据读取通过，66 项相关回归测试通过。
1.5.0-13 已完成 ARM64 原生构建、RPM 内容校验、安装及启动检查。
隔离 Wayland 协议测试验证主窗口与 cover 的可见性变化不请求退出，关闭事件请求结束宿主。
连续启动三次复用同一进程；最终真实关闭/重开/切后台的视觉行为仍待用户反馈。
