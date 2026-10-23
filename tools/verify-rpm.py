#!/usr/bin/env python3
"""Verify the actual Sailfish RPM payload, not a potentially stale build tree."""
from pathlib import Path, PurePosixPath
import json
import subprocess
import sys
import tempfile

package = Path(sys.argv[1]).resolve()

def rpm(*arguments):
    return subprocess.check_output(['rpm', *arguments, str(package)], text=True)

identity = rpm('-qp', '--qf', '%{NAME} %{VERSION} %{ARCH}').split()
assert identity == ['harbour-mconnect', '1.5.0', 'aarch64'], identity
files = rpm('-qlp').splitlines()
for name in files:
    assert '..' not in PurePosixPath(name).parts, name
bundle = '/usr/share/harbour-mconnect'
required = [
    '/usr/bin/harbour-mconnect',
    '/usr/share/applications/harbour-mconnect.desktop',
    bundle + '/harbour-mconnect',
    bundle + '/lib/libapp.so',
    bundle + '/lib/libmconnect_audio.so',
    bundle + '/data/flutter_assets/AssetManifest.bin',
]
for name in required:
    assert name in files, ('missing', name)
assert not any('libflutter_engine.so' in name or 'libflutter_elinux_' in name for name in files)
requires = rpm('-qpR')
assert 'flutter-sfos-3.44.0 = 3.44.0' in requires, requires
assert '/usr/share/fonts/wqy-zenhei/wqy-zenhei.ttc' in requires, requires
assert '/usr/share/fonts/sail-sans-pro/SailSansPro-Light.ttf' in requires, requires
assert '/usr/share/fonts/amiri/amiri-regular.ttf' in requires, requires
assert 'gtk-update-icon-cache' not in requires, requires
assert 'qt5-qtwayland-wayland_egl' in requires, requires
for library in ('libQt5Gui.so.5', 'libQt5DBus.so.5', 'libQt5Network.so.5', 'libQt5WaylandClient.so.5'):
    assert library in requires, (library, requires)

with tempfile.TemporaryDirectory(prefix='mconnect-rpm-check-') as temporary:
    producer = subprocess.Popen(['rpm2cpio', str(package)], stdout=subprocess.PIPE)
    extract = subprocess.run(['cpio', '-id', '--quiet', '--no-absolute-filenames'],
        cwd=temporary, stdin=producer.stdout, capture_output=True, text=True)
    producer.stdout.close()
    assert producer.wait() == 0
    assert extract.returncode == 0, extract.stderr
    root = Path(temporary)
    font_link = root / (bundle + '/data/flutter_assets/fonts/SailfishCJK.ttc').lstrip('/')
    assert font_link.is_symlink()
    assert str(font_link.readlink()) == '/usr/share/fonts/wqy-zenhei/wqy-zenhei.ttc'
    manifest = json.loads((font_link.parent.parent / 'FontManifest.json').read_text())
    assert {'Roboto', 'Ubuntu', 'Arial', 'sans-serif'} <= {entry['family'] for entry in manifest}
    by_family = {entry['family']: entry['fonts'] for entry in manifest}
    assert by_family['Sail Sans Pro Light'] == [{'asset': 'fonts/SailSansPro-Light.ttf'}]
    assert by_family['SailfishCJK'] == [{'asset': 'fonts/SailfishCJK.ttc'}]
    assert by_family['SailfishArabic'] == [{'asset': 'fonts/SailfishArabic.ttf'}]
    assert str((font_link.parent / 'SailSansPro-Light.ttf').readlink()) == '/usr/share/fonts/sail-sans-pro/SailSansPro-Light.ttf'
    assert str((font_link.parent / 'SailfishArabic.ttf').readlink()) == '/usr/share/fonts/amiri/amiri-regular.ttf'
    launcher = (root / 'usr/bin/harbour-mconnect').read_text()
    assert 'export EGL_PLATFORM=wayland' in launcher
    assert b'\r' not in (root / 'usr/bin/harbour-mconnect').read_bytes()
    for name in required[2:5]:
        header = (root / name.lstrip('/')).read_bytes()[:64]
        assert header[:4] == b'\x7fELF' and header[4] == 2, name
        assert int.from_bytes(header[18:20], 'little') == 183, ('not aarch64', name)
    service = root / 'usr/share/dbus-1/services/org.sailfishos.Mconnect.service'
    assert service.is_file()
    assert 'Exec=/usr/bin/harbour-mconnect --background' in service.read_text()
    assert 'phoneWindow=unmapped' in (root / 'usr/share/harbour-mconnect/harbour-mconnect').read_bytes().decode('latin1')
    desktop = (root / required[1].lstrip('/')).read_text()
    assert 'X-Sailplay-BackgroundService=org.sailfishos.Mconnect' in desktop
    assert 'Categories=AudioVideo;Audio;Music;Player;' in desktop
    assert 'Exec=harbour-mconnect' in desktop
    assert 'Icon=harbour-mconnect' in desktop
    launcher = (root / required[0].lstrip('/')).read_text()
    assert 'umask 077' in launcher
    assert '/flutter-sfos/$VER' in launcher

print(json.dumps({'rpm': str(package), 'identity': identity,
    'payload_files': len(files), 'aarch64_elf': 3,
    'runtime_dependency': 'flutter-sfos-3.44.0 = 3.44.0',
    'embedded_engine': False, 'result': 'PASS'}, indent=2))
