#!/bin/sh
set -eu

zypper --non-interactive refresh
zypper --non-interactive in --force-resolution \
	clang \
	wayland-devel \
	qt5-qtwayland-wayland_egl-devel \
	'pkgconfig(Qt5Gui)' \
	'pkgconfig(Qt5DBus)' \
	'pkgconfig(Qt5Network)' \
	'pkgconfig(Qt5WaylandClient)' \
	gstreamer1.0-devel \
	sqlite-devel \
	cmake \
	make \
	git \
	unzip \
	curl
