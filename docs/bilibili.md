# 哔哩哔哩音乐

根据工作区 `BiliMusic` 的 Web API 接入，作为第四个音乐来源。这里播放的是视频的 DASH 音频轨，不加载视频画面；UP 主显示在歌手位置。

支持搜索和分页、扫码登录及会话恢复、收藏夹歌单及完整翻页、添加/移除收藏夹歌曲、点赞/取消点赞、可用 AAC 音质、下载及离线播放。视频链接可作为分 P 歌单导入，收藏夹链接支持 `fid` / `media_id`。分 P 使用 `BV:cid` 稳定标识。

字幕在视频提供时转为时间轴歌词，来源标注为“哔哩哔哩字幕”，不保证视频具有字幕或歌词。暂不支持短信登录、短链接导入、创建/重命名/删除收藏夹、专辑和歌手主页、每日推荐与排行榜；点赞列表不映射成收藏夹。

签名密钥通过 nav 获取并缓存 12 小时；匿名 nav 的 `-101` 不代表 WBI 密钥不可用。搜索遇到 `v_voucher` 尝试备用搜索接口，仍被风控时显示验证提示。API 的权限及地域限制仍由 B 站决定。

登录 Cookie 仅用于 B 站 API，交给现有 SessionStorage 保存；不发送到音频或字幕 CDN。下载携带 Referer/User-Agent，保存为 M4A。Sailfish GStreamer 仅对 B 站媒体域设置播放 Referer，不改系统音量与路由。

验证：`flutter test --no-pub test/bilibili_platform_test.dart`。真实公开接口测试需显式设置 `BILIBILI_LIVE=1`，运行 `test/bilibili_live_test.dart`；该测试只读取音频片段，不播放声音。
