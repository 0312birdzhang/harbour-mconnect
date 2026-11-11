# Mconnect for SailfishOS

面向 **SailfishOS / aarch64** 的原生音乐应用，基于 Mconnect-Music_connect 1.5.0
改写，使用 C++ 业务核心、Silica/QML 界面和系统 GStreamer，发布格式是 RPM。
[上游项目](https://github.com/Hjdd14/Mconnect-Music_connect)

使用应用列表中的 **Mconnect** 启动。已接入四个平台的搜索、播放与歌词、
cover、账号和扫码登录、音乐库、发现、下载、缓存设置、本地音乐扫描、
歌手/专辑资料及本地歌单编辑和导入导出。当前实现与验证记录见迁移审计。
[docs/native-migration-audit.md](docs/native-migration-audit.md)

## 功能

保留原应用的网易云、QQ、酷狗适配器，并新增哔哩哔哩音乐来源。支持歌曲/歌单搜索、推荐、排行榜、
专辑与歌手、登录会话、歌单导入导出和管理、收藏、历史、队列、同步歌词、
本地扫描、下载与离线缓存。
这些是已移植的代码范围，平台接口、账号与实际播放仍需逐项真机验证。

业务实现位于 `elinux/silica/`，音频桥接位于 `elinux/runner/`，界面位于 `qml/`。

- 音频通过随 RPM 安装的 `libmconnect_audio.so` 调用系统 GStreamer playbin，
  使用 Sailfish `x-maemo` 系统媒体音量组，应用增益在独立软件音量节点处理。
  音频桥接支持播放、暂停、定位、音量、变速、均衡器。
- MPRIS 提供系统媒体元数据和播放/暂停/上一首/下一首/定位。
- 本地扫描支持指定音乐目录，歌单导入导出使用 JSON 文件。
- 在“音乐库 → 设置 → 下载目录”打开系统文件夹选择器。保存前会检查所选目录的写入权限。
- XDG 应用数据与缓存目录，SQLite 和兼容 Hive 2.2.3 的原生持久化。
- 使用 Wayland 与系统 Silica 字体，字体依赖由 RPM 声明。
- 音乐库与发现的子页面保留底部导航，播放小窗始终位于导航上方；点击当前标签返回其首页。
- 登录凭据采用权限为 0700 的目录和 0600 文件，保存在应用数据目录。
- 分享采用应用内复制/打开文件；浏览器与文件管理器通过 `xdg-open` 打开。
- Wi-Fi 下载策略检查内核默认路由；未知连接不视为 Wi-Fi。

Android 悬浮歌词、桌面小组件、Android Auto、Android 后台服务和均衡器通道
不适用于 Sailfish。外部 `mconnect://` 链接唤起尚未接入，歌单链接可以在应用内粘贴导入。
系统来电抢占、锁屏长时间播放、蓝牙路由、不同编码和登录接口必须以真机结果为准。

## 构建 RPM

使用 `D:\code\sfos-skills` 的 WSL SDK 构建脚本。当前配置为 `Ubuntu-22.04`、
SDK 用户 `pc` 和目标 `SailfishOS-latest-aarch64`。在本应用目录的 PowerShell 中执行以下命令。

```powershell
$env:WSL_DIST = 'Ubuntu-22.04'
$env:SDK_USER = 'pc'
$env:SFOS_TARGET = 'SailfishOS-latest-aarch64'
$env:WSL_BUILD_ROOT = '/home/pc/code/mconnect-native-migration'
$env:RPM_OUTPUT = (Join-Path (Get-Location).Path 'RPMS').Replace('\', '/')
& 'C:\Program Files\Git\bin\bash.exe' 'D:/code/sfos-skills/tools/sailfish-wsl-build.sh' '.'
```

产物为 `RPMS/harbour-mconnect-1.6.0-54.aarch64.rpm`。安装应用 RPM 后，从应用列表启动
Mconnect。两个桌面入口由系统启动器按权限配置运行在 Sailjail 内，后台 D-Bus
激活显式调用 Sailjail 启动原生 ELF。
桌面配置声明 `Internet`、`Audio`、`Music`、`Documents`、`Downloads` 和
`RemovableMedia` 权限，应用专属 profile 覆盖原生服务名称及既有数据、缓存目录。
SDK 路径、暂存目录和原生包布局见构建说明。
[elinux/README.md](elinux/README.md)

## 验证

在含有 `rpm`、`rpm2cpio`、`cpio`、`readelf` 和 Python 3 的环境中校验实际产物。

```sh
python3 tools/verify-rpm.py RPMS/harbour-mconnect-1.6.0-54.aarch64.rpm
```

原生实现的主机测试、SDK 构建和包校验结果记录在迁移审计中。`VALIDATION.md`
保存旧版本的主机与设备验证记录。本地状态位于
`~/.local/share/harbour-mconnect`，缓存位于 `~/.cache/harbour-mconnect`，
或相应的 XDG 环境变量目录。卸载 RPM 不会主动删除用户音乐或应用数据。

上游许可证保留于 `LICENSE`。仓库内的 Sailplay SDK 保留其 GPLv3+ 许可证，RPM 包含双方的许可证文件。

播放已有下载或缓存的歌曲时始终优先读取本地文件，无需打开“离线模式”；只有找不到有效文件时才尝试网络取流。首次播放会等待下载记录恢复完成。

正在播放页的歌词区在竖屏下占满标题栏与控制区之间的剩余空间；发现页推荐卡片与页面统一滚动，首页最多展示 9 首。短屏与大字模式保留滚动回退。

应用 cover 仅显示封面、最多两行歌名和播放/暂停、下一曲按钮；歌名根据其
背景图区域的亮度选择深浅字色。按钮连接现有 MPRIS 播放会话。
Silica 入口使用原生 QML 主窗口和 cover，C++ 业务核心在同一进程中运行。
原生按钮/状态回归可通过 `MCONNECT_BUILD_COVER_TESTS=ON` 构建
`mconnect_cover_test`，在设备上以 `dbus-run-session -- ./mconnect_cover_test` 运行；
测试使用隔离的模拟播放服务，不会播放音乐。

哔哩哔哩音乐基于 [BiliMusic](https://github.com/eason919191/BiliMusic) 的 Web API 适配；支持范围见 [docs/bilibili.md](docs/bilibili.md)。

当前 RPM 版本为 `1.6.0-54`。音乐库“设置”下方的“关于”页面显示应用图标、
名称、版本、开发者 BirdZhang、许可证及 Mconnect-Music_connect、BiliMusic
参考项目。点击参考项目打开其 GitHub 页面。版本从 RPM 构建信息读取，横屏
采用左右两栏，内容随页面滚动。页面下方显示四段免责声明，正文自动换行。

探索页使用单一搜索页面，通过并排的 ComboBox
选择音乐平台和歌曲、歌单类型。切换筛选时沿用当前关键词重新搜索，返回
探索页时恢复平台、类型、关键词和结果。
公共页头在正向竖屏时按系统刘海高度为标题和按钮预留顶部空间。
底部四个导航按钮和背景整体上移 `Theme.paddingLarge`。页面跟随系统旋转，
播放器横屏时在左侧显示封面或歌词，右侧显示播放控制，内容过长时可以滚动。
横屏探索页将平台、类型、搜索框和搜索按钮放在同一排，空状态提示位于控件下方。
播放器的下拉菜单由整页顶部承载。歌曲入队成功后返回主界面的下载页并显示全部
任务，页面切换期间等待导航完成。下载完成后的导航在下载表单处于前台时执行。

cover 的两个操作按钮各有一个紧贴图标的
`#808080` 灰色圆底，整体大小为系统小图标的 1.5 倍。左、右按钮各向外移
`Theme.paddingSmall`，显示尺寸按 cover 大小补偿缩放。透明 PNG 按钮由
cover 页面绘制，点击通过 `CoverActionList` 触发。
关闭手机窗口会释放 QML 页面。播放、下载等后台任务和车载会话保留业务核心，任务与所有会话结束后经过十秒空闲期退出。
窗口生命周期协议测试可用 `MCONNECT_BUILD_COVER_TESTS=ON` 构建
`mconnect_window_lifecycle_test`，使用隔离的模拟播放服务。
