#!/bin/sh
# From the Flutter app root:
#   ./elinux/build.sh          # flutter-elinux build + RPM inside sfosbuild
#   ./elinux/build.sh deploy
#   ./elinux/build.sh clean
#
# Needs:
#   FLUTTER_SFOS_RPMS  dir with flutter-sfos-<ver> and -devel RPMs
set -eu

cd "$(CDPATH= cd -- "$(dirname "$0")/.." && pwd)"
ELINUX=$PWD/elinux
if [ -z "${FLUTTER_VERSION:-}" ]; then
	# shellcheck disable=SC1091
	. "$ELINUX/flutter-sfos.env"
fi
: "${FLUTTER_VERSION:?set FLUTTER_VERSION or elinux/flutter-sfos.env}"

RPMS=${FLUTTER_SFOS_RPMS:-$PWD/../../versions/$FLUTTER_VERSION/runtime/rpms}
SFOS=${SFOS:-5.1.0.11}
ARCH=${ARCH:-aarch64}
DEVICE=${DEVICE:-}

if [ "${1:-}" = clean ]; then
	rm -rf build "$ELINUX"/rpms "$ELINUX"/out "$ELINUX"/bundle \
		"$ELINUX"/build-native rpms
	rm -f ./*.rpm "$ELINUX"/*.rpm
	for f in "$ELINUX"/*.in rpm/*.in; do
		[ -f "$f" ] && rm -f "${f%.in}"
	done
	exit 0
fi

if ! command -v sfosbuild >/dev/null 2>&1; then
  echo "Install sfosbuild and start Docker before building the Sailfish RPM" >&2
  exit 1
fi
if [ ! -d "$RPMS" ]; then
  echo "Missing runtime RPMs: $RPMS; build runtime with make FLUTTER=$FLUTTER_VERSION runtime" >&2
  exit 1
fi
test -d rpm
test -x "$ELINUX/build.sh"

for f in "$ELINUX"/*.in rpm/*.in; do
	[ -f "$f" ] || continue
	sed "s|@FLUTTER_VERSION@|$FLUTTER_VERSION|g" "$f" > "${f%.in}"
done
chmod +x "$ELINUX"/*.sh

rm -f ./flutter-sfos-"$FLUTTER_VERSION"-*.rpm
cp -f "$(ls -t "$RPMS"/flutter-sfos-"$FLUTTER_VERSION"-"$FLUTTER_VERSION"-*.aarch64.rpm | grep -v debug | head -1)" \
	"$(ls -t "$RPMS"/flutter-sfos-"$FLUTTER_VERSION"-devel-"$FLUTTER_VERSION"-*.aarch64.rpm | grep -v debug | head -1)" \
	./

# In-place rpmbuild packages this tree. Drop a previous arch's build first.
if [ -f Makefile ]; then
	make clean
	if make -n distclean >/dev/null 2>&1; then
		make distclean
	fi
fi
if [ -f build/elinux/Makefile ]; then
	make -C build/elinux clean
	if make -C build/elinux -n distclean >/dev/null 2>&1; then
		make -C build/elinux distclean
	fi
fi
rm -rf build

sfosbuild --in-place "$SFOS" "$ARCH" .

if [ "${1:-}" = deploy ]; then
	: "${DEVICE:?set DEVICE=user@device-address before deploying}"
	sfosbuild deploy "$DEVICE" .
fi
