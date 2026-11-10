#!/bin/sh
set -eu
umask 077
exec /usr/bin/sailjail -p harbour-mconnect-silica.desktop -- /usr/bin/harbour-mconnect-silica --background "$@"
