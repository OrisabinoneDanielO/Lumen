# Packaging

Planned installers: MSIX/MSI (Windows), signed and notarised .dmg (macOS), AppImage, Flatpak, .deb and .rpm (Linux).

## Linux: AppImage (batch 12)

One file that runs on most Linux systems:

```sh
cmake --preset release && cmake --build --preset release
packaging/linux/build-appimage.sh   # writes build/appimage/Lumen-Presenter-<version>-x86_64.AppImage
```

The script installs the build into an AppDir, adds `linux/lumen-presenter.desktop` and the icon, and bundles Qt (including Qt Multimedia's FFmpeg backend, Qt PDF and the Wayland and X11 platforms) with [linuxdeploy](https://github.com/linuxdeploy/linuxdeploy) and its Qt plugin. It expects their AppImages in `build/tools` (or set `LINUXDEPLOY` and `LINUXDEPLOY_PLUGIN_QT`).

An AppImage only runs on systems whose glibc is at least as new as the build machine's, so release AppImages are built in CI on Ubuntu 22.04 (the `appimage` job), not on a developer's newest Fedora.

## Not yet

- Windows and macOS installers, Flatpak, .deb and .rpm: chosen to wait (batch 12 builds only the AppImage). Unsigned Windows and macOS builds will warn on first start (SmartScreen, Gatekeeper) until code-signing certificates are bought.
