#!/bin/sh
# Run from the application checkout inside the Sailfish build environment.
# ./elinux/build.sh builds the native RPM. deploy and clean retain their names.
set -eu

cd "$(CDPATH= cd -- "$(dirname "$0")/.." && pwd)"
SFOS=${SFOS:-5.1.0.11}
ARCH=${ARCH:-aarch64}
DEVICE=${DEVICE:-}

if [ "${1:-}" = clean ]; then
    rm -rf -- build/native rpms elinux/rpms elinux/out elinux/bundle elinux/build-native
    rm -f -- rpm/harbour-mconnect.yaml elinux/harbour-mconnect.sh elinux/harbour-mconnect.desktop
    exit 0
fi

if ! command -v sfosbuild >/dev/null 2>&1; then
    echo "Install sfosbuild and start its build engine before building the Sailfish RPM" >&2
    exit 1
fi
cp rpm/harbour-mconnect.yaml.in rpm/harbour-mconnect.yaml
cp elinux/harbour-mconnect.sh.in elinux/harbour-mconnect.sh
cp elinux/harbour-mconnect.desktop.in elinux/harbour-mconnect.desktop
chmod +x elinux/harbour-mconnect.sh elinux/harbour-mconnect-silica.sh elinux/harbour-mconnect-core.sh
# Native CMake configuration has its own directory across repeated builds.
sfosbuild --in-place "$SFOS" "$ARCH" .

if [ "${1:-}" = deploy ]; then
    : "${DEVICE:?set DEVICE=user@device-address before deploying}"
    sfosbuild deploy "$DEVICE" .
fi
