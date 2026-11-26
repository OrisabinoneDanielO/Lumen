# Packaging

| System | Installer | Made by | Qt |
| --- | --- | --- | --- |
| Windows | `.msi` | CPack (WiX Toolset 3) | bundled by `windeployqt` |
| macOS | `.dmg` with `Lumen Presenter.app` | CPack (DragNDrop) | bundled by `macdeployqt` |
| Linux | AppImage | `linux/build-appimage.sh` (linuxdeploy) | bundled |
| Linux | Flatpak | `flatpak/io.github.OrisabinoneDanielO.Lumen.yml` | KDE runtime 6.8 |
| Linux | `.deb`, `.rpm` | CPack | the distribution's (Qt 6.5 or newer) |

The [Packages workflow](../.github/workflows/packages.yml) builds every one of them for each change to `main`, for `v*` tags, for pull requests that touch packaging, and for branches named `packaging/...`, installs it on a clean machine or container, and runs `lumen --check-install`. That checks the operator window loads, the bundled Bibles open, and the renderer starts and draws its first frame, then exits with an error if anything failed. Try it after installing anywhere:

```sh
lumen --check-install                                                # Linux
"/Applications/Lumen Presenter.app/Contents/MacOS/Lumen Presenter" --check-install
"C:\Program Files\Lumen Presenter\lumen.exe" --check-install > check.txt   # Windows (a GUI program: redirect to see the output)
```

## Making a release

1. Set the new version in `CMakeLists.txt` (`project(... VERSION x.y.z)`) and add a `<release>` line to `linux/lumen-presenter.metainfo.xml`.
2. Commit, then tag and push: `git tag -a vX.Y.Z -m "Lumen Presenter X.Y.Z" && git push origin vX.Y.Z`.
3. The Packages workflow builds, installs and checks every installer, then makes a **draft** release with all of them attached and notes from the merged changes.
4. Read the draft on the Releases page, edit the notes, and click **Publish release**.

## Where things go

| | Windows | macOS | Linux |
| --- | --- | --- | --- |
| `lumen`, `lumen-render` | `C:\Program Files\Lumen Presenter\` | `Lumen Presenter.app/Contents/MacOS/` | `/usr/bin/` |
| `bibles.db` | beside `lumen.exe` | `Contents/Resources/` | `/usr/share/lumen/` |
| Qt | beside `lumen.exe` | `Contents/Frameworks`, `PlugIns`, `Resources/qml` | the system's |

`lumen` finds `lumen-render` beside itself and `bibles.db` in these places (`app/main.cpp`, `RenderLink::findExecutable`). The layout is set in the top-level `CMakeLists.txt` (`LUMEN_INSTALL_BINDIR`, `LUMEN_INSTALL_DATADIR`); `packaging/CMakeLists.txt` bundles Qt and configures CPack.

## Building them

After a release build (`cmake --preset release -DLUMEN_BUILD_TESTS=OFF && cmake --build --preset release`):

```sh
cpack --config build/release/CPackConfig.cmake -B build/packages            # this system's default: .msi, .dmg
cpack --config build/release/CPackConfig.cmake -B build/packages -G DEB     # needs dpkg-dev
cpack --config build/release/CPackConfig.cmake -B build/packages -G RPM     # needs rpm-build
```

- **Windows** needs [WiX Toolset 3](https://github.com/wixtoolset/wix3/releases) (`choco install wixtoolset`). The upgrade code in `packaging/CMakeLists.txt` must never change: it lets a newer `.msi` replace an older one.
- **macOS**: the bundle is signed ad hoc. With a Developer ID certificate in the keychain, configure with `-DLUMEN_CODESIGN_IDENTITY="Developer ID Application: …"` to sign with the hardened runtime (`macos/entitlements.plist`), then notarise the `.dmg` with `xcrun notarytool submit … --wait` and `xcrun stapler staple`.
- **.deb and .rpm** are built against the distribution's own Qt, on the distribution they are for (CI uses Debian 13 and the latest Fedora). The `.deb` lists the QML modules and Qt plugins Lumen loads at run time in `CPACK_DEBIAN_PACKAGE_DEPENDS`. Ubuntu 24.04 has Qt 6.4, older than Lumen needs, so it uses the AppImage or the Flatpak. Fedora's FFmpeg (`ffmpeg-free`) leaves out some patented codecs such as H.264; install `ffmpeg` from RPM Fusion to play those videos.
- **Flatpak**: `flatpak-builder --user --install --install-deps-from=flathub build/flatpak packaging/flatpak/io.github.OrisabinoneDanielO.Lumen.yml`, then `flatpak run io.github.OrisabinoneDanielO.Lumen`. It uses Qt's WebEngine base app for Qt PDF. The permissions it asks for are explained in the manifest.

## Linux: AppImage (batch 12)

One file that runs on most Linux systems:

```sh
cmake --preset release && cmake --build --preset release
packaging/linux/build-appimage.sh   # writes build/appimage/Lumen-Presenter-<version>-x86_64.AppImage
```

The script installs the build into an AppDir, adds `linux/lumen-presenter.desktop` and the icon, and bundles Qt (including Qt Multimedia's FFmpeg backend, Qt PDF and the Wayland and X11 platforms) with [linuxdeploy](https://github.com/linuxdeploy/linuxdeploy) and its Qt plugin. It expects their AppImages in `build/tools` (or set `LINUXDEPLOY` and `LINUXDEPLOY_PLUGIN_QT`).

An AppImage only runs on systems whose glibc is at least as new as the build machine's, so release AppImages are built in CI on Ubuntu 22.04 (the Packages workflow's `appimage` job), not on a developer's newest Fedora.

## Icons

`icons/lumen-presenter.ico` (Windows, 16–256 px) and `icons/lumen-presenter.icns` (macOS) are made from `linux/lumen-presenter.svg`. To remake them after changing the SVG:

```sh
for n in 16 24 32 48 64 128 256 1024; do
  magick -background none -density $((n*3/2)) packaging/linux/lumen-presenter.svg -resize ${n}x${n} PNG32:icon-$n.png
done
python3 -c "from PIL import Image; i=[Image.open(f'icon-{n}.png') for n in (16,24,32,48,64,128,256)]; i[-1].save('packaging/icons/lumen-presenter.ico', append_images=i[:-1], sizes=[(n,n) for n in (16,24,32,48,64,128,256)]); Image.open('icon-1024.png').save('packaging/icons/lumen-presenter.icns')"
```

## Not yet

- **Code signing**: Windows (an Authenticode certificate) and macOS (an Apple Developer ID, and notarisation). Until then SmartScreen and Gatekeeper warn on the first start.
- **MSIX**, **apt and dnf repositories**, and a **Flathub** submission.
