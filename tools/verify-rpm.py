#!/usr/bin/env python3
"""Verify the native Sailfish RPM's extracted payload and runtime dependencies."""
from pathlib import Path, PurePosixPath
import hashlib
import json
import re
import subprocess
import sys
import tempfile

package = Path(sys.argv[1]).resolve()


def rpm(*arguments):
    return subprocess.check_output(['rpm', *arguments, str(package)], text=True)


identity = rpm('-qp', '--qf', '%{NAME} %{VERSION} %{RELEASE} %{ARCH}').split()
specification = (Path(__file__).resolve().parents[1] / 'rpm/harbour-mconnect.spec').read_text()
expected_identity = [re.search(r'^' + field + r':\s*(\S+)', specification, re.MULTILINE).group(1)
                     for field in ('Name', 'Version', 'Release')] + ['aarch64']
assert identity == expected_identity, identity
files = rpm('-qlp').splitlines()
for name in files:
    assert '..' not in PurePosixPath(name).parts, name
    assert not any(part == 'flutter_assets' for part in PurePosixPath(name).parts), name
    assert not name.endswith(('.dart', '.dill', '/libapp.so')), name
    assert 'libflutter' not in name.lower(), name
bundle = '/usr/share/harbour-mconnect'
native_bundle = '/usr/libexec/harbour-mconnect'
required = [
    '/etc/sailjail/permissions/harbour-mconnect.profile',
    '/etc/sailjail/permissions/harbour-mconnect-silica.profile',
    '/usr/bin/harbour-mconnect',
    '/usr/bin/harbour-mconnect-silica',
    '/usr/libexec/harbour-mconnect-core',
    '/usr/share/applications/harbour-mconnect.desktop',
    '/usr/share/applications/harbour-mconnect-silica.desktop',
    '/usr/share/dbus-1/services/org.sailfishos.Mconnect.service',
    '/usr/share/licenses/harbour-mconnect/LICENSE',
    '/usr/share/licenses/harbour-mconnect/qrcodegen-LICENSE',
    bundle + '/harbour-mconnect',
    bundle + '/mconnect-silica',
    bundle + '/lib/libmconnect_audio.so',
    native_bundle + '/mconnect-silica',
    native_bundle + '/lib/libmconnect_audio.so',
]
qml_files = [
    'main.qml', 'GroupSlideshow.qml', 'MainShellPage.qml', 'AppHeader.qml',
    'AppFooter.qml', 'PlatformTabs.qml', 'PlaylistSourcesPage.qml', 'SearchPage.qml',
    'PlayerPage.qml', 'QueuePage.qml', 'MiniPlayer.qml', 'cover/CoverPage.qml',
    'LibraryPage.qml', 'AccountsPage.qml', 'LoginPage.qml', 'LibraryPlaylistsPage.qml',
    'LibrarySongsPage.qml', 'CreatePlaylistDialog.qml', 'DiscoveryPage.qml',
    'ChartsPage.qml', 'DownloadsPage.qml', 'DownloadSongPage.qml', 'SettingsPage.qml', 'AboutPage.qml',
    'LocalMusicPage.qml', 'ScanDirectoryDialog.qml', 'SongActionsPage.qml', 'ArtistPage.qml',
    'ArtistAlbumsPage.qml', 'AlbumPage.qml', 'AddToPlaylistPage.qml', 'PlaylistTransferPage.qml',
]
required += [bundle + '/qml/' + name for name in qml_files]
cover_icons = {
    'cover-play.png': '05941dca3f7695b31a8a62fef105b43d6766130be98c5b8b9eb62694fc63aa48',
    'cover-pause.png': 'a0420c0975dd491e1849d8c17a939404218f7e921ffbb261667b0dc0c7034fd5',
    'cover-next-song.png': 'f8b07204dbc38495b4646c26d151c99f9590f15fa334c72cac21ee6a98fe4943',
    'cover-action-transparent.png': 'f2bb5bbaca678ecad746b1fa5ecfa2c8a81dd18817be19f0187c036d25326317',
}
required += [bundle + '/qml/cover/icons/' + name for name in cover_icons]
for name in required:
    assert name in files, ('missing', name)
requires = rpm('-qpR')
assert 'flutter' not in requires.lower(), requires
assert 'gtk-update-icon-cache' not in requires, requires
for requirement in (
    'sailjail', 'qt5-qtwayland-wayland_egl', 'sailfishsilica-qt5', 'sailfish-components-pickers-qt5',
    'libQt5Quick.so.5', 'libQt5Qml.so.5', 'libQt5Gui.so.5',
    'libQt5DBus.so.5', 'libQt5Network.so.5', 'libsqlite3.so',
    'libgstreamer-1.0.so', 'libtag.so',
    '/usr/share/fonts/wqy-zenhei/wqy-zenhei.ttc',
    '/usr/share/fonts/sail-sans-pro/SailSansPro-Light.ttf',
    '/usr/share/fonts/amiri/amiri-regular.ttf',
    '/usr/share/fonts/notoemoji/NotoEmoji-Regular.ttf',
    '/usr/share/fonts/dejavu/DejaVuSans.ttf',
):
    assert requirement in requires, (requirement, requires)

with tempfile.TemporaryDirectory(prefix='mconnect-rpm-check-') as temporary:
    root = Path(temporary)
    # GNU cpio needs the parent directory before extracting the first symlink.
    (root / 'usr/bin').mkdir(parents=True)
    producer = subprocess.Popen(['rpm2cpio', str(package)], stdout=subprocess.PIPE)
    extract = subprocess.run(['cpio', '-id', '--quiet', '--no-absolute-filenames'],
        cwd=temporary, stdin=producer.stdout, capture_output=True, text=True)
    producer.stdout.close()
    assert producer.wait() == 0
    assert extract.returncode == 0, extract.stderr
    license_directory = root / 'usr/share/licenses/harbour-mconnect'
    assert 'Copyright (c) 2026 Mconnect' in (license_directory / 'LICENSE').read_text()
    assert 'Copyright (c) Project Nayuki.' in (license_directory / 'qrcodegen-LICENSE').read_text()
    for size in (86, 108, 128, 172):
        icon = root / f'usr/share/icons/hicolor/{size}x{size}/apps/harbour-mconnect.png'
        header = icon.read_bytes()[:26]
        assert header[:8] == b'\x89PNG\r\n\x1a\n', icon
        assert int.from_bytes(header[16:20], 'big') == size, icon
        assert int.from_bytes(header[20:24], 'big') == size, icon
        assert header[25] == 6, ('icon must have transparency', icon)
    launcher = root / 'usr/bin/harbour-mconnect'
    native_launcher = root / 'usr/bin/harbour-mconnect-silica'
    core_launcher = root / 'usr/libexec/harbour-mconnect-core'
    assert launcher.is_symlink() and str(launcher.readlink()) == 'harbour-mconnect-silica'
    assert launcher.resolve() == native_launcher.resolve()
    assert not native_launcher.is_symlink()
    assert b'\r' not in core_launcher.read_bytes()
    assert 'umask 077' in core_launcher.read_text()
    assert ('exec /usr/bin/sailjail -p harbour-mconnect-silica.desktop -- '
            '/usr/bin/harbour-mconnect-silica --background "$@"') in core_launcher.read_text()
    assert 'flutter' not in core_launcher.read_text().lower()
    compatibility = root / 'usr/share/harbour-mconnect/harbour-mconnect'
    assert compatibility.is_symlink() and str(compatibility.readlink()) == 'mconnect-silica'
    for name, target in (
        ('mconnect-silica', '../../libexec/harbour-mconnect/mconnect-silica'),
        ('lib/libmconnect_audio.so', '../../../libexec/harbour-mconnect/lib/libmconnect_audio.so'),
    ):
        link = root / bundle.lstrip('/') / name
        assert link.is_symlink() and str(link.readlink()) == target, (name, target)
        assert link.resolve().is_relative_to(root.resolve()), link
    legacy_executable = root / 'usr/libexec/harbour-mconnect/mconnect-silica'
    assert legacy_executable.is_symlink()
    assert str(legacy_executable.readlink()) == '../../bin/harbour-mconnect-silica'
    assert legacy_executable.resolve() == native_launcher.resolve()
    executables = ['/usr/bin/harbour-mconnect-silica', native_bundle + '/lib/libmconnect_audio.so']
    for name in executables:
        executable = root / name.lstrip('/')
        header = executable.read_bytes()[:64]
        assert header[:4] == b'\x7fELF' and header[4] == 2, name
        assert int.from_bytes(header[18:20], 'little') == 183, ('not aarch64', name)
        dynamic = subprocess.check_output(['readelf', '-d', str(executable)], text=True)
        assert 'libflutter' not in dynamic.lower(), name
    native_path = native_launcher
    dynamic = subprocess.check_output(['readelf', '-d', str(native_path)], text=True)
    assert '$ORIGIN/../libexec/harbour-mconnect/lib' in dynamic, dynamic
    assert 'libmconnect_audio.so' in dynamic, dynamic
    service = root / 'usr/share/dbus-1/services/org.sailfishos.Mconnect.service'
    assert 'Name=org.sailfishos.Mconnect' in service.read_text()
    assert ('Exec=/usr/bin/sailjail -p harbour-mconnect-silica.desktop -- '
            '/usr/bin/harbour-mconnect-silica --background') in service.read_text()
    compatibility_desktop = root / 'usr/share/applications/harbour-mconnect.desktop'
    assert 'NoDisplay=true' in compatibility_desktop.read_text()
    assert 'ExecDBus=' not in compatibility_desktop.read_text()
    desktop = root / 'usr/share/applications/harbour-mconnect-silica.desktop'
    assert 'ExecDBus=/usr/bin/harbour-mconnect-silica --background' in desktop.read_text()
    assert 'X-Sailplay-ApplicationId=harbour-mconnect' in desktop.read_text()
    for application in ('harbour-mconnect', 'harbour-mconnect-silica'):
        desktop = root / f'usr/share/applications/{application}.desktop'
        text = desktop.read_text()
        for value in (
            f'Exec=/usr/bin/{application}',
            'X-Nemo-Single-Instance=no',
            'X-Sailplay-BackgroundService=org.sailfishos.Mconnect',
            'X-Sailplay-ApiVersion=1', 'X-Sailplay-Service=org.sailfishos.Mconnect',
            'X-Sailplay-ObjectPath=/org/sailplay/v1/App',
            'X-Sailplay-Presentation=template', 'X-Sailplay-Capabilities=media;content;',
            'Categories=AudioVideo;Audio;Music;Player;', 'Icon=harbour-mconnect',
            'X-Nemo-Application-Type=generic', '[X-Sailjail]',
            'OrganizationName=org.sailfishos', 'ApplicationName=Mconnect',
            'DataDirectory=harbour-mconnect',
            'Permissions=Internet;Audio;Music;Documents;Downloads;RemovableMedia;',
        ):
            assert value in text, (desktop, value)
        assert 'Sandboxing=Disabled' not in text, desktop
    profile = root / 'etc/sailjail/permissions/harbour-mconnect.profile'
    silica_profile = root / 'etc/sailjail/permissions/harbour-mconnect-silica.profile'
    assert silica_profile.read_text() == 'include /etc/sailjail/permissions/harbour-mconnect.profile\n'
    profile_lines = profile.read_text().splitlines()
    for member in ('org.sailplay.Session1.StateChanged', 'org.sailplay.Session1.RequestFinished',
                   'org.sailplay.Navigation1.GuidanceChanged', 'org.freedesktop.DBus.Properties.PropertiesChanged'):
        assert 'dbus-user.call org.sailplay.Client.*=' + member + '@/org/sailplay/v1/*' in profile_lines
    for value in (
        'dbus-user.own org.sailfishos.Mconnect.Core',
        'dbus-user.own org.sailfishos.Mconnect.Silica',
        'dbus-user.call com.nokia.profiled=com.nokia.profiled.get_profile@/com/nokia/profiled',
        'dbus-user.call com.nokia.profiled=com.nokia.profiled.get_profiles@/com/nokia/profiled',
        'dbus-user.call com.nokia.profiled=com.nokia.profiled.get_value@/com/nokia/profiled',
        'dbus-user.broadcast com.nokia.profiled=com.nokia.profiled.profile_changed@/com/nokia/profiled',
        'mkdir ${HOME}/.local/share/harbour-mconnect',
        'whitelist ${HOME}/.local/share/harbour-mconnect',
        'mkdir ${HOME}/.cache/harbour-mconnect',
        'whitelist ${HOME}/.cache/harbour-mconnect',
    ):
        assert value in profile_lines, value
    qml = root / 'usr/share/harbour-mconnect/qml'
    header = (qml / 'AppHeader.qml').read_text()
    assert 'property alias headerPage: nativeHeader.page' in header
    assert 'Screen.topCutout.height' in header
    assert 'height: titles.height + 2 * Theme.paddingLarge + topInset' in header
    assert header.count('anchors.verticalCenterOffset: header.topInset / 2') == 2
    assert 'x: Theme.horizontalPageMargin\n' in header
    assert 'headerBackButton' not in header
    assert 'navigationCapsule' in (qml / 'AppFooter.qml').read_text()
    assert 'footerBackdrop' in (qml / 'AppFooter.qml').read_text()
    assert 'SlideshowView {' in (qml / 'GroupSlideshow.qml').read_text()
    assert 'sharedRootMenu' in (qml / 'MainShellPage.qml').read_text()
    assert 'rootPageFlickable' in (qml / 'MainShellPage.qml').read_text()
    search = (qml / 'SearchPage.qml').read_text()
    assert search.count('ComboBox {') == 2
    assert 'objectName: "searchPlatform"' in search
    assert 'objectName: "searchMode"' in search
    assert 'objectName: "searchPlaceholder"' in search
    assert 'Math.max(selectors.height, searchInput.height)' in search
    assert 'list.headerItem.height' in search
    assert 'PlatformTabs' not in search
    assert 'exploreGroup' not in (qml / 'MainShellPage.qml').read_text()
    assert 'PullDownMenu' not in (qml / 'SearchPage.qml').read_text()
    assert 'PullDownMenu' not in (qml / 'DiscoveryPage.qml').read_text()
    assert 'QueuePage.qml' not in (qml / 'MiniPlayer.qml').read_text()
    assert 'initialPage: Component { MainShellPage {} }' in (qml / 'main.qml').read_text()
    library = (qml / 'LibraryPage.qml').read_text()
    assert library.index('libraryAboutEntry') > library.index('librarySettingsEntry')
    assert 'app.openUnique("AboutPage.qml", "aboutPage", {})' in library
    about = (qml / 'AboutPage.qml').read_text()
    for value in (
        'Qt.application.version', 'text: "Mconnect"', 'text: "BirdZhang"',
        '/usr/share/icons/hicolor/172x172/apps/harbour-mconnect.png',
        'https://github.com/Hjdd14/Mconnect-Music_connect',
        'https://github.com/eason919191/BiliMusic', 'Qt.openUrlExternally(projectUrl)',
        'page.isLandscape', 'Text.WrapAnywhere',
        'objectName: "aboutDisclaimerText"', 'textFormat: Text.PlainText', 'wrapMode: Text.Wrap',
        '本项目为个人学习与自用开发的第三方客户端，与网易云音乐、QQ 音乐、酷狗音乐官方无任何关联。',
        '不提供、不存储、不分发任何音乐内容；所有内容版权归各平台与版权方所有。',
        '登录凭据只保存在本机（系统安全存储），仓库与日志中不含任何真实账号信息。',
        '接口随时可能调整，功能以实际可用为准。使用产生的任何后果由使用者自行承担。',
    ):
        assert value in about, value
    player = (qml / 'PlayerPage.qml').read_text()
    assert 'objectName: "playerPageFlickable"' in player
    assert 'objectName: "playerMenu"' in player
    assert 'width: playerScroll.width' in player
    download = (qml / 'DownloadSongPage.qml').read_text()
    assert 'app.openDownloads()' in download
    assert 'page.enqueued && page.status === PageStatus.Active' in download
    assert 'pageStack.replace' not in download
    assert 'function openDownloads()' in (qml / 'main.qml').read_text()
    settings = (qml / 'SettingsPage.qml').read_text()
    assert 'import Sailfish.Pickers 1.0' in settings
    assert 'FolderPickerPage {' in settings
    assert 'onSelectedPathChanged:' in settings
    assert 'app.call("setDownloadDirectory", {path: path}' in settings
    assert 'TextField {' not in settings
    cover = (qml / 'cover/CoverPage.qml').read_text()
    assert 'coverActionBackground' not in cover
    assert cover.count('CoverAction {') == 2
    assert cover.count('iconSource: Qt.resolvedUrl("icons/cover-action-transparent.png")') == 2
    for value in (
        'iconBackground: false', 'enabled: core.ready && !!core.state.song',
        '1.5 * Theme.iconSizeSmall / coverScale', 'Theme.paddingSmall / coverScale',
        'size === Cover.Small ? Theme.coverSizeSmall.width : Theme.coverSizeLarge.width',
        'x: parent.width / 4 - width / 2 - cover.actionOffset',
        'x: 3 * parent.width / 4 - width / 2 + cover.actionOffset',
        'anchors.bottomMargin: coverActionRow.height + Theme.paddingMedium',
        'Qt.resolvedUrl("icons/cover-" + (core.state.playing ? "pause" : "play") + ".png")',
        'Qt.resolvedUrl("icons/cover-next-song.png")',
        'core.request("toggle", {})', 'core.request("next", {})',
    ):
        assert value in cover, value
    for name, expected_digest in cover_icons.items():
        icon = qml / 'cover/icons' / name
        assert hashlib.sha256(icon.read_bytes()).hexdigest() == expected_digest, icon
    native = native_path.read_bytes()
    for marker in (
        b'MCONNECT_NATIVE_CORE_STARTED', b'MCONNECT_NATIVE_BACKGROUND_READY',
        b'org.sailplay.Content1', b'content.browse', b'content.search', b'content.selectSong',
        b'org.mpris.MediaPlayer2.harbour_mconnect', b'/org/mpris/MediaPlayer2/track/t',
        b'/org/sailfishos/Mconnect/Core', b'/org/sailfishos/Mconnect/Platforms',
        b'/org/sailfishos/Mconnect/Artwork', b'loginBegin', b'localScan',
        b'downloadEnqueue', b'importPlaylist', b'playResult', b'NativePlayer',
        b'MCONNECT_NATIVE_QQ_UTF8_REPAIRED', b'MCONNECT_NATIVE_QQ_CATALOG_OK',
        b'MCONNECT_NATIVE_KUGOU_CATALOG_OK', b'MCONNECT_NATIVE_BILIBILI_OK',
        b'MCONNECT_NATIVE_NETEASE_ACCOUNT_OK', b'MCONNECT_NATIVE_NETEASE_CATALOG_OK',
        b'MCONNECT_NATIVE_NETEASE_CONTENT_OK', b'MCONNECT_NATIVE_NETEASE_SEARCH_OK',
        b'MCONNECT_NATIVE_ARTWORK_DISK_HIT', b'MCONNECT_NATIVE_ARTWORK_NETWORK_FETCH',
        b'SILICA_SEARCH_SELECTORS_PASS', b'SILICA_SEARCH_SINGLE_PANE_PASS',
        b'SILICA_HEADER_CUTOUT_PASS',
        b'SILICA_SEARCH_LAYOUT_PASS', b'SILICA_PLAYER_MENU_PASS',
        b'SILICA_DOWNLOAD_NAVIGATION_PASS', b'SILICA_VALIDATION_ISOLATED_CORE_PASS',
        b'SILICA_ABOUT_CONTENT_PASS', b'SILICA_ABOUT_NAVIGATION_PASS',
        b'MCONNECT_NATIVE_LYRICS_CACHE_HIT', b'GetPlayListDetail', b'GetUserFavSongList',
        b'GetVipInfo', b'AddSongFav', b'DeleteSongFav', b'/node/musicmac/v6/index.html',
        b'/api/v3/singer/album', b'/lyric/fcgi-bin/fcg_query_lyric_new.fcg',
        b'/rsc/fcgi-bin/fcg_get_profile_homepage.fcg', b'/rsc/fcgi-bin/fcg_user_created_diss',
        b'/splcloud/fcgi-bin/fcg_music_add2songdir.fcg', b'/splcloud/fcgi-bin/create_playlist.fcg',
        b'/folder/fcgi-bin/fcg_qm_order_diss.fcg',
    ):
        assert marker in native, marker
    assert b'MCONNECT_SILICA_RECOVERED_CORE' not in native

print(json.dumps({'rpm': str(package), 'identity': identity, 'payload_files': len(files),
    'aarch64_elf': 2, 'runtime': 'native-cpp', 'sailjail_profiles': 2, 'result': 'PASS'}, indent=2))
