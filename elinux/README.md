# Native Sailfish build

Mconnect uses C++ business services, GStreamer playback and a Silica/QML
interface. The application RPM is built through the native CMake targets in
this directory. Current migration evidence and remaining validation are in
[the migration audit](../docs/native-migration-audit.md).

## Build environment

Use the WSL SDK workflow in `D:\code\sfos-skills`. The verified configuration
uses `Ubuntu-22.04`, SDK user `pc`, SDK root `/srv/sailfishos/sdks/sfossdk`,
environment `/home/pc/.hadk.env` and target `SailfishOS-latest-aarch64`.
The native targets require
Qt Core, Gui, DBus, Network, Qml and Quick, sailfishapp, SQLite, zlib, TagLib,
and GStreamer. QR encoding uses the bundled MIT-licensed Nayuki generator.
Build dependencies are declared in `rpm/harbour-mconnect.yaml.in`.

From the application root in PowerShell, run the following commands.

```powershell
$env:WSL_DIST = 'Ubuntu-22.04'
$env:SDK_USER = 'pc'
$env:SFOS_TARGET = 'SailfishOS-latest-aarch64'
$env:WSL_BUILD_ROOT = '/home/pc/code/mconnect-native-migration'
$env:RPM_OUTPUT = (Join-Path (Get-Location).Path 'RPMS').Replace('\', '/')
& 'C:\Program Files\Git\bin\bash.exe' 'D:/code/sfos-skills/tools/sailfish-wsl-build.sh' '.'
```

The helper synchronizes into the isolated WSL directory
`/home/pc/code/mconnect-native-migration/mconnect`, runs `mb2` in the SDK chroot
and exports RPMs into `RPMS`. Synchronization uses `rsync --delete`, so this
staging directory must be dedicated to the build. Use forward slashes for
the Windows `RPM_OUTPUT` path.

Keep `rpm/harbour-mconnect.yaml` synchronized with its `.yaml.in` template.
Spectacle retains the custom CMake build and install commands and changelog
in `rpm/harbour-mconnect.spec`. CMake builds in `build/native` and installs
through the RPM buildroot. The package identity is
`harbour-mconnect-1.6.0-54.aarch64`. The target's `/etc/os-release` reports
Sailfish OS `5.1.0.11`; runtime dependency versions are recorded in the audit.

## Package verification

Run the verifier in an environment containing `rpm`, `rpm2cpio`, `cpio`,
`readelf` and Python 3, supplying the actual generated RPM path.

```sh
python3 tools/verify-rpm.py /path/to/harbour-mconnect-1.6.0-54.aarch64.rpm
```

The package installs the native ELF as `/usr/bin/harbour-mconnect-silica` and
`libmconnect_audio.so` into `/usr/libexec/harbour-mconnect/lib`. The executable uses
`$ORIGIN/../libexec/harbour-mconnect/lib` for its audio library. Relative symlinks expose both files at
their existing paths under `/usr/share/harbour-mconnect`. The bundle executable
`harbour-mconnect` links to `mconnect-silica`. Both `/usr/bin/harbour-mconnect` and
`/usr/libexec/harbour-mconnect/mconnect-silica` also resolve to the native ELF.
Both desktop entries use the system launcher's Sailjail integration, and
background activation uses the visible `harbour-mconnect-silica.desktop`
profile and its declared `ExecDBus` command. It explicitly invokes Sailjail with
`org.sailfishos.Mconnect` as the application identity. Matching profiles in
`/etc/sailjail/permissions` grant the native Core and Silica D-Bus names and
access to the existing `~/.local/share/harbour-mconnect` and
`~/.cache/harbour-mconnect` directories. The application and bundled Sailplay
SDK license notices install into `/usr/share/licenses/harbour-mconnect`.

The bundled SDK source, canonical D-Bus XML and GPLv3+ notice are in
`elinux/sailplay-sdk`. Native builds use that source overlay by default.

Native regression targets can be enabled with `-DMCONNECT_BUILD_TESTS=ON` or
the existing `-DMCONNECT_BUILD_COVER_TESTS=ON` CMake option. Package generation,
installed runtime, playback and visual validation have separate evidence in
the migration audit.
