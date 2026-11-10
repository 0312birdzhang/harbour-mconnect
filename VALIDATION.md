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
真机截图：[最终首屏](docs/device-final.png)、[搜索与输入法](docs/device-search.png)。

后续还需验证：QQ/酷狗搜索与播放、各平台登录、同步歌词、锁屏长时间播放、
来电抢占、蓝牙/耳机路由及下载后的离线播放。账号与会员限制按平台响应处理；
此次网易云测试遇到会员歌曲时，应用显示相应错误并跳过。

`upstream_platform_tests/` 保留但不计入默认测试集的 Android/Windows 专属测试。

## 1.5.0-2 标题中文修复

补充 Ubuntu、Arial、sans-serif 字体资源别名，覆盖没有指定字体的标题样式。
此前仅注册 Roboto，正文正常但 AppBar 等独立样式仍会缺字。
此次未更改 Dart/AOT 和音频逻辑，重新生成字体清单并打包。

真机截图确认：[下载管理](docs/device-download-r2.png)、
[音乐库「我喜欢」](docs/device-likes-r2.png)、
[发现「榜单中心」](docs/device-toplist-r2.png)标题中文正常。
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

## 1.5.0-14 歌词字体与启动策略

歌词高亮的 DefaultTextStyle/AnimatedDefaultTextStyle 继承原有字体与回退，不再仅保留字号和字重。
逐字颜色边界对齐完整 grapheme，避免拆开扩展汉字、组合字符和 emoji。标题与正文增加系统 Noto Emoji / DejaVu Symbols 回退。
35 项歌词、进度及导航测试通过，额外标题回退测试通过；合计 36 项不同检查。静态分析无问题。
设备字体路径完整；只读线程栈未发现死锁。观察到系统单实例 invoker 等待，而应用本身已有 flock/Show 单实例处理。
Desktop Entry 设置 X-Nemo-Single-Instance=no，避免两套机制冲突；实际缺字及无响应的用户场景仍需设备反馈确认。

最终 RPM 已通过 40 文件/3 ARM64 ELF、四个回退字体别名、系统字体依赖、LF 启动脚本及 X-Nemo-Single-Instance=no 校验。
1.5.0-14 真机安装及 rpm -V 通过；普通前台启动成功，日志确认 playback core ready，未发起播放。
歌词实际显示及再次点击图标时的用户场景仍待反馈，不将启动检查视为缺字或卡死的视觉验收。

## 1.5.0-15 海报及歌词缓存

新增私有 MCA 海报封装、完整性校验、同 URL 请求合并、旧缓存按需迁移、网络海报 LRU 清理；列表/播放器/迷你播放器共用缓存。
本地音乐内嵌封面也保存为 MCA；系统 MPRIS/cover 通过回环解码端点读取，不落普通图片文件。
歌词默认优先使用可解析缓存，按当前歌曲后台预取，并保存 LRCLIB 等备用来源及来源标签。
134 项相关测试通过，静态分析 0 问题；全量 ARM64 AOT/原生构建、40 文件/3 ARM64 ELF 的 RPM 校验通过。
真机安装 1.5.0-15、rpm -V 通过。检查得到 1 份 MCA 文件，摘要正确，新私有海报目录中普通图片数为 0；数据库有 7 条歌词记录。
重启后日志确认 yrc 歌词缓存命中，没有再次调用歌词平台接口。未播放音乐，未截图；隔离测试验证新实例在离线条件下读取海报与歌词缓存。


## 2026-10-31 — Silica migration milestone 1 (1.5.0-16)

- Added an independent Sailfish Silica application window and native cover,
  connected to the retained Dart/Riverpod core over a versioned session D-Bus API.
- Native preview covers four-platform song search, result playback commands,
  queue, current track/position, line lyrics with translations, mini player,
  and cover controls. Native login/library/discovery/download/settings remain
  later milestones; this is not feature parity.
- Host: 20 bridge/adapter/MPRIS/artwork/lyrics tests passed. Targeted static
  analysis and main entry analysis report zero issues.
- Full ARM64 AOT and native CMake RPM build passed. Actual RPM verification:
  50 payload files, 4 ARM64 ELF files, expected Qt/Silica/runtime dependencies,
  LF launcher, no embedded Flutter engine.
- Device: installed 1.5.0-16; rpm -V clean. Installed QML validation instantiated
  all 6 components. No QML errors matched in the native process journal.
- Read-only live searches: NetEase 30, Kugou 30, Bilibili 26; QQ 0. QQ's existing
  adapter returns an empty list when its response has no song list; no native
  QQ search success is claimed for this run.
- With user approval to interrupt the observed playback, SIGTERM of the native
  UI exited both UI and its owned core. Reopen restored 31 queued songs and 66
  lyric lines, paused. A second launch exited successfully, raised the existing
  instance and left playback paused. Final instance UI PID 19646 / core 19649.
- No screenshots or automated audio playback were used. Layout appearance and
  audible playback are subject to user-directed verification.


## 2026-10-31 — Silica migration milestone 2 (1.5.0-17)

- Native library entry, account management, four-platform QR login routing,
  local and platform playlist pages, likes/history, paged song lists, play-all,
  local playlist creation, and adding the current song to a local playlist.
- Reuses AuthNotifier/session storage, MyPlaylistsRepository and existing
  platform adapters. Native startup now wires session-expiry reporting.
- QR attempts have bounded lifetime and generation/session guards. Closing a
  page during QR acquisition invalidates its result; stale cancellation cannot
  cancel a newer attempt. QR keys and cookies are not exposed to QML.
- Host: 29 bridge/library/login/MPRIS/cache tests plus 3 targeted downloaded-file
  playback regressions passed. Static analysis reports zero issues.
- Complete Dart ARM64 AOT build passed. Native validation-harness/parent guards
  were subsequently compiled incrementally with production CMake and repackaged
  with that same AOT. Source/package verification: 56 files, 4 ARM64 ELF files,
  expected runtime dependencies, no embedded Flutter engine, LF launcher.
- Installed 1.5.0-17 and rpm -V clean. Installed native validation reports
  SILICA_IMAGE_DATA_URL_PASS and SILICA_QML_VALIDATION_PASS components=12.
  Dialog type is compiled; its instance requires the real page stack. Other
  components instantiate in the harness. No QML errors matched in native UI
  PID 20372's actual startup journal.
- Device read-only data: 2 local playlists (first has 49 songs), 0 local likes,
  85 history entries, 2 NetEase playlists (first has 49 songs), 7 Bilibili folders.
  Bilibili first-folder 105 tracks returned in pages of 100 and 5.
- Restored NetEase and Bilibili accounts. On the logged-out Kugou adapter,
  QR acquisition/render returned a valid PNG and waiting status, exposed neither
  key nor cookie, and cancel removed the attempt. No live account was logged out.
- Final state paused with 31 queue entries and 66 lyric lines. No automated audio
  playback or screenshots. Real QR scan/authorization, page appearance and user
  interaction acceptance remain user-directed verification.
- Final RPM SHA256:
  929745591fd8784d59df6bfab6dce4ebcf5538311eed5b21d54a8019f8678914


## 2026-10-31 — Native search input method (1.5.0-18)

- Confirmed running 1.5.0-17 native UI lacked QT_IM_MODULE and did not map the
  Maliit input-context plugin, although the session exports QT_IM_MODULE=Maliit.
- Native entry now defaults QT_IM_MODULE to Maliit before QGuiApplication
  creation, preserving an explicit supplied value. Search requests focus after
  page activation and shows the input method when its field gains active focus.
- Production CMake native rebuild and RPM verification passed. Reuses the
  unchanged, previously validated Dart AOT core. Installed 1.5.0-18, rpm -V clean;
  installed PNG/QML validation passed.
- Actual replacement UI PID 20967 maps
  /usr/lib64/qt5/plugins/platforminputcontexts/libmaliitplatforminputcontextplugin.so.
  Startup restored paused state (31 queue entries, 66 lyric lines).
- No screenshots or playback commands. Visible keyboard and typing acceptance
  require the user's interaction with the search field.


## 2026-10-31 — Silica migration milestone 3 (1.5.0-19)

- Added native discovery (supported recommendations/charts/new songs), download
  management and song download entry points, cache settings and shuffle/repeat.
  Recommendation provenance and chart periods are retained. Existing notifier,
  transport, membership checks, queue and file resolution remain authoritative.
- Download rows use a keyed incremental model; page-level cancellation remorse
  survives progress updates. Polling stops while the app is not foreground.
- Host: 35 bridge/login/library/discovery/download/cache tests, one additional
  playback-mode test, and 34 existing resume/queue-policy/cache-setting tests
  passed (70 distinct checks). The five-test adapter rerun includes four already
  counted checks plus the additional playback-mode check. Static analysis clean.
- Full ARM64 Dart AOT and native production CMake build passed. Actual RPM
  verification: 61 payload files, 4 ARM64 ELF files, expected dependencies, LF
  launcher and no embedded engine. Installed 1.5.0-19; rpm -V clean.
- Installed no-window validator passed memory PNG loading, 17 QML component
  types, and actual Qt model insert/update/move/remove with persistent-index
  identity checks. The dialog type is compiled rather than mounted outside a
  page stack. No matching QML errors in native UI PID 21922's startup journal.
- Device read-only live data: NetEase 32 recommended songs (source 每日推荐),
  65 chart entries and 100 new songs; QQ 30 chart entries. Unsupported Bilibili
  discovery categories are hidden and search/favorites remain available.
- Download snapshot restored 70 existing tasks (68 completed, 2 failed).
  Cache settings retained 2304 MB, wifiOnly/autoRetry/autoCleanup true and
  offlineMode false. No real download was enqueued or user setting changed by
  device probes; mutation/pause/resume behavior uses fake transport in tests.
- Actual native process maps the Maliit input plugin. Final player state paused,
  31 queued songs and 66 lyric lines. No automated music playback or screenshots;
  visual layout and user-triggered real download acceptance remain unverified.
- Final RPM SHA256:
  216786b65f62231240dcfbc76af44406ba09f80c20dfd29e78d2f15e1362a47a

## 2026-10-31 — Silica migration milestone 4 (1.5.0-20)

- Added native indexed local music, explicit isolate-backed directory scanning,
  artist/album detail navigation, local favorites and local playlist editing,
  copying, reordering, import and export. Original Song objects and the shared
  download-first playback resolver are retained. Local edits are serialized.
- Host: 48 core-transfer/bridge/local-music/media/playlist/login/library/discovery/
  download/artwork/lyrics/MPRIS tests passed. Local scan tests use real FLAC and
  LRC fixtures with the scanner isolate, verify saved roots and index reuse,
  and reject overlapping scans. Playlist tests use isolated storage and verify
  concurrent edits, ordering, sharing and preservation of original audio files.
  Targeted static analysis passed with no issues.
- Fixed partial large writes in the existing Dart D-Bus transport by sending
  bounded replies through expiring base64 chunks. A 360021-byte Unicode result
  passed the production native CoreBridge receiver on an isolated phone D-Bus
  session (NATIVE_CORE_TRANSFER_PASS); Dart service tests also round-trip a large
  Unicode result. No dependency source was modified.
- Staged phone no-window validation passed 25 QML component types, memory PNG
  loading and keyed download-model operations. Dialog types compile outside a
  real page stack; other components instantiate. An unsupported Silica TextArea
  property was removed based on the target device's loader error.
- Full ARM64 AOT and production native CMake build passed. Actual RPM verification
  passed: 69 payload files, 4 ARM64 ELF files, required QML pages, system fonts,
  LF launcher and expected runtime dependencies without an embedded engine.
  Installed harbour-mconnect-1.5.0-20.aarch64; rpm -V clean. Installed validator
  passed all 25 QML types plus PNG and keyed-model checks.
- Device read-only local index: 91 tracks, 3 albums, 4 artists, 2 folders, no
  scan in progress and no index error. No real directory scan was triggered.
  Existing local playlist export returned a 14359-character link without
  changing clipboard or playlist contents. NetEase search supplied valid media
  IDs; artist detail/artwork, 50 top songs, 30 albums, album detail/artwork and
  26 album tracks returned successfully. Existing download state retained
  70 tasks (68 completed, 2 failed).
- Native UI PID 24860 maps Maliit. No matching fatal launch-file errors. The
  regular-user journal check lacked journal permissions and did not establish
  journal-level QML validation. Final state paused, 31 queue entries and
  66 lyric lines. No automated playback, screenshots, actual user-playlist
  mutation or phone directory scan. Layout, input interaction, clipboard flow
  and real scan/playback acceptance remain user-directed checks.
- Final RPM SHA256:
  60941855ce470020e3f2d89b163f447dda10879ff2c1914afa4d3b5a280c056e

## 2026-10-31 — Native desktop launch repair (1.5.0-21)

- User reported the Silica icon did not open. Direct SSH launches had bypassed
  desktop launch restrictions. The system journal records Sailjail rejecting
  the original /usr/share executable: "Legacy apps must be in: /usr/bin/" and
  "Command line does not match template".
- Added an LF /usr/bin/harbour-mconnect-silica wrapper. The desktop file uses
  the generic invoker and explicitly disables Sailjail, matching the existing
  business core's launch model. Silica/QML still renders the native interface;
  generic selects process startup rather than the UI toolkit. Directly forcing
  the sailjail CLI on this shell wrapper is unsupported (not an ELF binary);
  the desktop entry's disabled-sandbox generic invoker path is the tested path.
- Repacked unchanged release-20 AOT/native binaries with the launcher repair.
  Actual RPM verification passed: 70 payload files, 4 ARM64 ELF files, launcher
  and desktop assertions. Installed 1.5.0-21; rpm -V clean.
- Device generic invoker returned 0. Closed the paused UI, then launched again:
  native PID 29237 and owned core PID 29363 restored paused with 31 queued songs.
  Repeated invoker launch retained the same two PIDs. Maliit mapped in the UI.
  A root-authorized journal check found no matching QML errors for UI PID 29237.
  No audio playback or screenshots. Physical icon tapping remains user acceptance.
- RPM SHA256:
  5b9b81691da14965b70976844ebde10ab700e6ec2d34449f3270cb70eb4a844c

## 2026-10-31 — Flutter layout alignment (1.5.0-22)

- Aligned the native app with Flutter's four tabs, nested-page footer, mini
  player above the navigation capsule, compact titles/back buttons, platform and
  song/playlist search selectors, library entry order, discovery shortcuts and
  nine-card recommendation scroll. Added download list filters and direct
  player shuffle/repeat/queue access. Search query and paged results survive
  tab replacement. Full player defaults to cover and can switch to lyrics.
- Retained original Song records, download-first playback, MCA artwork cache,
  platform capability checks, recommendation provenance, native input setup and
  release-21 generic desktop launcher. Added only searchPlaylists/resultArtwork
  bridge operations; no platform adapter or audio implementation was rewritten.
- Host: 21 adapter/discovery/library/artwork/D-Bus transfer tests passed, including
  paged playlist search identity and coverless result handling without audio.
  Targeted static analysis passed. A first test command referenced a nonexistent
  core_service_test.dart; corrected to sailfish_core_service_test.dart and the
  full selected suite passed.
- Device staging: 29 QML component types passed, plus memory PNG, keyed download
  model operations, a nine-card fixture with scroll content/footer-reservation
  checks, and all four root routes with nested-page reset and footer existence.
  Tests do not render screenshots or establish visual acceptance. The hidden
  fixture uses actual Silica content geometry and effective visibility rather
  than assuming its outer Page size determines its content viewport.
- Corrected device theme icon names and Qt header-component ID scope for search
  and local grouping. No QML errors remained in the final staged validation.
- Full ARM64 AOT/native production build passed. Actual RPM verification passed:
  74 payload files, 4 ARM64 ELF files, all new components, navigation capsule,
  Explore initial page, release-21 launcher and external runtime dependencies.
  Installed harbour-mconnect-1.5.0-22.aarch64; rpm -V clean. Installed no-window
  validation repeated all 29 types, PNG/model/grid/navigation checks successfully.
- Desktop generic invoker returned 0 and a second invocation retained native
  PID 29502 / core PID 30640. Native UI maps Maliit. Root-authorized journal
  filtering found no matching QML type/reference/layout/image errors for this UI.
  Final state paused with 31 queued tracks. Read-only NetEase requests returned
  30 songs and 30 playlists with correct platform identity; result artwork was
  read successfully through the private loopback cache endpoint.
- No screenshots, automatic playback, user playlist mutations or physical
  keyboard input. Actual appearance, tapping/typing and playback remain user
  acceptance. The component/geometry checks are not visual approval.
- Final RPM SHA256:
  77d10769240fc9d9fcdb65ef10adab2f1b32274676c224e1be610d8467e1a648

## 2026-10-31 — Cyclic SlideshowView shell (1.5.0-23)

- Implemented the requested 12-pane sequence, cyclic in both directions, using
  native SlideshowView. Root platform/filter selectors and footer destinations
  select panes in the same shell. Footer is outside the clipped slide content,
  with an opaque backdrop; embedded root bodies hide their duplicate footer.
- Removed Discovery/Library account pulley entries and the mini-player queue
  icon. Player/queue helpers locate and return to existing stack entries.
- Staged target-device validation passed 30 component types, all 12 pane bodies
  and platform/filter IDs, forward/backward wrap, nested root reset, and repeated
  player/queue opens with stable depths of 2/3 rather than unbounded pushes.
  Existing PNG, keyed download-model and nine-card scroll checks also passed.
- Initial old-Qt invalid-context warnings were reproduced without component
  fixtures. Moving page Components from recycled delegates into the shell's
  context removed the warnings. Only the active pane creates its pulley menu,
  preventing inactive neighbors from registering an active menu. Final full and
  shell-only staged validations completed without those QML warnings.
- No Dart/audio code changed. Before update, installed release-22 libapp.so SHA256:
  0cc01d6c2bccdab3f1b5c113c4c3923b5ac3b9836650a426e9ec69b41c41a6c3.
- Production native CMake rebuild and actual RPM verification passed: 75 payload
  files, 4 ARM64 ELF files, MainShellPage/SlideshowView, opaque footer backdrop,
  removed mini-player queue route, and the preserved generic desktop launcher.
  Reuses the unchanged release-22 AOT. Installed 1.5.0-23; rpm -V clean; libapp.so
  SHA256 after install matches the value above exactly.
- Installed no-window validation passed all 30 types plus opaque/clipped footer,
  nine-card scroll, keyed model, 12-pane platform/filter order, wrap in both
  directions, nested reset and player/queue reuse checks. No invalid-context
  warnings in this final installed validation.
- Generic desktop launch and repeated launch returned 0 and retained UI PID
  31377 / core PID 37474. Native UI maps Maliit. Root-authorized journal filtering
  found no matching QML type/reference/layout/image/context errors for that UI.
  Final state paused with 31 queued songs. No screenshots or automated playback;
  actual finger swipes, appearance and typing remain user-directed acceptance.
- Final RPM SHA256:
  26f14b504c47b3d5a162ada2ab4131e05c4689f55e899969323fa0cadf3bccd2

## 2026-10-31 — Independent page groups (1.5.0-24)

- Replaced the global 12-pane carousel with independent Explore/Discovery/
  Library/Downloads groups of 4/4/1/3. Library does not slide. Only bottom-tab
  actions change the primary page. Titles, selectors and one shared menu stay
  outside the page-local SlideshowView; embedded bodies hide their duplicates.
- Staged device checks passed 31 component types, each group index/platform/
  filter mapping, forward/backward local wrap with unchanged tab, rejection of
  out-of-range indices, and shared-menu target identity. Existing opacity/clip,
  grid/model, nested navigation and unique player/queue checks also passed.
- Initial shared-menu null-Flickable errors were reproduced on target Qt/Silica.
  A persistent parking Flickable and delayed menu binding removed those errors.
  Final staged validation completed without matching QML errors or context
  warnings. No Dart/audio code changed; reuses the release-22 AOT core.
- Production native rebuild and actual RPM checks passed: 76 payload files,
  4 ARM64 ELF files, independent GroupSlideshow, shared menu, opaque footer and
  preserved generic launcher. Installed 1.5.0-24; rpm -V clean. Installed validator
  passed all 31 types and additionally verified remembered indices across tab
  changes. No matching QML errors in installed validation.
- Generic desktop launch returned 0; UI PID 37662 maps Maliit and owned core PID
  41719 restored paused with 31 queued songs. Installed libapp.so retains SHA256
  0cc01d6c2bccdab3f1b5c113c4c3923b5ac3b9836650a426e9ec69b41c41a6c3.
  No screenshots, automatic playback or physical swipe/input acceptance.
- RPM SHA256:
  74e87c026c0fc8b4b3fb0f314df7da4de4b4983018a7780632a903fd34de4ab2

## 2026-11-01 — Outer menu/cache/recovery repair (1.5.0-25)

- Read-only failure evidence: native UI PID 37662 had disappeared, while core
  PID 41719 remained paused (31 songs) and was adopted by PID 3565. The old native
  startup guard rejects any existing org.sailfishos.Mconnect owner, explaining
  failed reopen. Available journal retained about 55 seconds and contained no
  original UI crash stack; the original crash cause is not proven.
- Removed all Explore/Discovery index-body pulley code. One outer Flickable owns
  the shared menu permanently, replacing cross-list reparenting. Coordinated
  recommendations in the window, with durable per-platform/account .mcr metadata,
  concurrent request joining, manual refresh and failure fallback. Added guarded
  recovery of verified native cores and parent-death protection for fresh cores.
- Host: 25 recommendation/cache/discovery/adapter/playback-memory/D-Bus tests
  passed. Coverage includes concurrent joining, cache read after recreation,
  preserved artist/album IDs, account separation, explicit refresh, failed refresh,
  corruption, retry and recommendation tokens surviving ordinary result pressure.
  Targeted static analysis passed with no issues.
- Target native CoreProcess test passed identity/birth guards and parent-death
  cleanup using /bin/sleep children (no audio). Staged 31-component validation
  passed fixed outer-menu target identity, page-local 4/4/1/3 groups, local wrap,
  remembered indices, unique player/queue, opaque footer and grid/model checks.
- Full ARM64 AOT/native production build and actual RPM verification passed:
  76 payload files, 4 ARM64 ELF files, fixed root Flickable, no pulley menus in
  Search/Discovery source and native recovery marker. Installed 1.5.0-25;
  rpm -V clean. Installed 31-component validation repeated successfully.
- Recovered the actual orphan PID 41719 without pre-killing it: native UI PID
  42688 opened paused, and journal recorded MCONNECT_SILICA_RECOVERED_CORE.
  Normal UI shutdown then stopped both UI and old core. Fresh start produced
  UI 58449/core 58649. NetEase returned 32 recommendations; first request wrote
  metadata and the next returned the local-cache note.
- After confirming paused, SIGKILL of UI 58449 also terminated core 58649 via
  parent-death protection. Reopen succeeded as UI 58646/core 59358, paused with
  31 queued tracks. The first recommendation request in this new core hit the
  persisted 32-song cache. Cache directory mode 0700, .mcr file mode 0600,
  15595 bytes, .nomedia present.
- Root-authorized journal filtering found no matching QML type/reference/layout/
  image/context errors for final UI 58646. No screenshots or automated playback.
  Original spontaneous-crash causation remains unproven without its lost stack;
  physical pulley/swipe behavior and long-run stability remain user acceptance.
- Final RPM SHA256:
  a13db7e4696b85495ff2358494b0c0d0b86eee0487994701ca6044e520885b4a
