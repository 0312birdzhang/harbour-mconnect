#!/bin/sh
set -eu

zypper --non-interactive refresh
zypper --non-interactive in --force-resolution \
	clang \
	wayland-devel \
	qt5-qtwayland-wayland_egl-devel \
	'pkgconfig(sailfishapp)' \
	'pkgconfig(Qt5Qml)' \
	'pkgconfig(Qt5Quick)' \
	'pkgconfig(Qt5Gui)' \
	'pkgconfig(Qt5DBus)' \
	'pkgconfig(Qt5Network)' \
	'pkgconfig(Qt5WaylandClient)' \
	'pkgconfig(zlib)' \
	'pkgconfig(taglib)' \
	gstreamer1.0-devel \
	sqlite-devel \
	gcc-c++ \
	cmake \
	make \
	python3-base \
	git \
	unzip \
	curl
