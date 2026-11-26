#!/bin/sh
# Builds Lumen-Presenter-<version>-x86_64.AppImage from a release build.
#
#   cmake --preset release && cmake --build --preset release
#   packaging/linux/build-appimage.sh            # writes into build/appimage
#
# Needs linuxdeploy and linuxdeploy-plugin-qt (AppImages from
# github.com/linuxdeploy) in build/tools, or set LINUXDEPLOY and
# LINUXDEPLOY_PLUGIN_QT. Qt comes from the build's own Qt (qmake6 on PATH,
# or set QMAKE). The AppImage runs on Linux systems with a glibc at least as
# new as the build machine's, so release builds should be made on an old
# distribution (CI uses Ubuntu 22.04).
set -eu

root=$(cd "$(dirname "$0")/../.." && pwd)
build=${BUILD_DIR:-$root/build/release}
out=${OUT_DIR:-$root/build/appimage}
tools=$root/build/tools
linuxdeploy=${LINUXDEPLOY:-$tools/linuxdeploy-x86_64.AppImage}
plugin=${LINUXDEPLOY_PLUGIN_QT:-$tools/linuxdeploy-plugin-qt-x86_64.AppImage}
export QMAKE=${QMAKE:-$(command -v qmake6 || command -v qmake)}
# AppImages can run without FUSE (containers, CI).
export APPIMAGE_EXTRACT_AND_RUN=1

version=$(sed -n '/^project(LumenPresenter/,/)/s/^ *VERSION \([0-9.]*\).*/\1/p' "$root/CMakeLists.txt")
appdir=$out/AppDir
rm -rf "$appdir"
mkdir -p "$out"

cmake --install "$build" --prefix "$appdir/usr"
install -Dm644 "$root/packaging/linux/lumen-presenter.desktop" "$appdir/usr/share/applications/lumen-presenter.desktop"
install -Dm644 "$root/packaging/linux/lumen-presenter.svg" "$appdir/usr/share/icons/hicolor/scalable/apps/lumen-presenter.svg"

# The Qt plugin finds the QML modules both programs import, and bundles
# Qt Multimedia's FFmpeg backend and the Wayland and X11 platforms.
export QML_SOURCES_PATHS="$root/app/ui:$root/render"
export EXTRA_PLATFORM_PLUGINS="libqwayland-generic.so;libqxcb.so;libqoffscreen.so"
# Plugin hooks run inside the AppImage; plain installs need no help.
cd "$out"
LDAI_OUTPUT="Lumen-Presenter-$version-x86_64.AppImage" VERSION="$version" \
"$linuxdeploy" --appdir "$appdir" \
    --executable "$appdir/usr/bin/lumen" \
    --executable "$appdir/usr/bin/lumen-render" \
    --desktop-file "$appdir/usr/share/applications/lumen-presenter.desktop" \
    --icon-file "$appdir/usr/share/icons/hicolor/scalable/apps/lumen-presenter.svg" \
    --plugin qt \
    --output appimage
echo "Wrote $out/Lumen-Presenter-$version-x86_64.AppImage"
