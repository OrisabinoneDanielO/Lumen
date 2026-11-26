# Installers (see packaging/README.md):
#
#   cmake --build --preset release
#   cpack --config build/release/CPackConfig.cmake -B build/packages            # this OS's default
#   cpack --config build/release/CPackConfig.cmake -B build/packages -G DEB     # or RPM
#
# Windows makes an .msi (WiX), macOS a .dmg, Linux a .deb or .rpm. On Windows
# and macOS the install step copies Qt in beside the program (windeployqt,
# macdeployqt); the Linux packages use the distribution's Qt.

set(LUMEN_SOURCE_QML_DIRS ${CMAKE_SOURCE_DIR}/app/ui ${CMAKE_SOURCE_DIR}/render)

if(WIN32)
    # The Visual C++ runtime DLLs, beside the program.
    set(CMAKE_INSTALL_SYSTEM_RUNTIME_DESTINATION ${LUMEN_INSTALL_BINDIR})
    set(CMAKE_INSTALL_UCRT_LIBRARIES TRUE)
    include(InstallRequiredSystemLibraries)

    if(TARGET Qt6::windeployqt)
        set(windeployqt "$<TARGET_FILE:Qt6::windeployqt>")
    else()
        find_program(windeployqt windeployqt HINTS "${QT6_INSTALL_PREFIX}/${QT6_INSTALL_BINS}" REQUIRED)
    endif()
    set(qmldirs)
    foreach(dir IN LISTS LUMEN_SOURCE_QML_DIRS)
        string(APPEND qmldirs " --qmldir \"${dir}\"")
    endforeach()
    install(CODE "
        set(dir \"\$ENV{DESTDIR}\${CMAKE_INSTALL_PREFIX}/${LUMEN_INSTALL_BINDIR}\")
        message(STATUS \"Bundling Qt into \${dir}\")
        execute_process(
            COMMAND \"${windeployqt}\" --no-translations --no-compiler-runtime --no-system-d3d-compiler
                    ${qmldirs} --dir \"\${dir}\" \"\${dir}/lumen.exe\" \"\${dir}/lumen-render.exe\"
            RESULT_VARIABLE result)
        if(result)
            message(FATAL_ERROR \"windeployqt failed: \${result}\")
        endif()")

elseif(APPLE)
    if(TARGET Qt6::macdeployqt)
        set(macdeployqt "$<TARGET_FILE:Qt6::macdeployqt>")
    else()
        find_program(macdeployqt macdeployqt HINTS "${QT6_INSTALL_PREFIX}/${QT6_INSTALL_BINS}" REQUIRED)
    endif()
    set(qmldirs)
    foreach(dir IN LISTS LUMEN_SOURCE_QML_DIRS)
        string(APPEND qmldirs " \"-qmldir=${dir}\"")
    endforeach()
    # Signing: ad hoc unless an identity is given (a "Developer ID
    # Application" certificate in the keychain), which also turns on the
    # hardened runtime that notarisation needs.
    set(LUMEN_CODESIGN_IDENTITY "" CACHE STRING "macOS signing identity (empty: ad hoc)")
    if(LUMEN_CODESIGN_IDENTITY)
        set(codesign_args "--options runtime --timestamp --entitlements \"${CMAKE_CURRENT_SOURCE_DIR}/macos/entitlements.plist\" --sign \"${LUMEN_CODESIGN_IDENTITY}\"")
    else()
        set(codesign_args "--sign -")
    endif()
    install(CODE "
        set(bundle \"\$ENV{DESTDIR}\${CMAKE_INSTALL_PREFIX}/${LUMEN_BUNDLE_NAME}.app\")
        message(STATUS \"Bundling Qt into \${bundle}\")
        execute_process(
            COMMAND \"${macdeployqt}\" \"\${bundle}\" ${qmldirs}
                    \"-executable=\${bundle}/Contents/MacOS/lumen-render\" -always-overwrite
            RESULT_VARIABLE result)
        if(result)
            message(FATAL_ERROR \"macdeployqt failed: \${result}\")
        endif()
        # macdeployqt rewrites the binaries, so they are signed again.
        execute_process(
            COMMAND codesign --force --deep ${codesign_args} \"\${bundle}\"
            RESULT_VARIABLE result)
        if(result)
            message(FATAL_ERROR \"codesign failed: \${result}\")
        endif()")

else()
    install(FILES linux/lumen-presenter.desktop DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)
    install(FILES linux/lumen-presenter.svg DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/scalable/apps)
    install(FILES linux/lumen-presenter.metainfo.xml DESTINATION ${CMAKE_INSTALL_DATADIR}/metainfo
            RENAME io.github.OrisabinoneDanielO.Lumen.metainfo.xml)
endif()

# ---- CPack ------------------------------------------------------------------

configure_file(${CMAKE_SOURCE_DIR}/LICENSE ${CMAKE_BINARY_DIR}/LICENSE.txt COPYONLY)

set(CPACK_PACKAGE_NAME "lumen-presenter")
set(CPACK_PACKAGE_VENDOR "Lumen Presenter contributors")
set(CPACK_PACKAGE_CONTACT "Lumen Presenter contributors <https://github.com/OrisabinoneDanielO/Lumen/issues>")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "Free, offline worship presentation: lyrics, scripture, media and slides")
set(CPACK_PACKAGE_DESCRIPTION
    "Lumen Presenter shows song lyrics, scripture, media and slides on the screens of a church service. It is free, works without internet, keeps the audience output running even if the operator window fails, and bundles freely licensed Bible translations.")
set(CPACK_PACKAGE_HOMEPAGE_URL "https://github.com/OrisabinoneDanielO/Lumen")
set(CPACK_PACKAGE_VERSION ${PROJECT_VERSION})
set(CPACK_PACKAGE_INSTALL_DIRECTORY "${LUMEN_BUNDLE_NAME}")
set(CPACK_RESOURCE_FILE_LICENSE ${CMAKE_BINARY_DIR}/LICENSE.txt)
set(CPACK_PACKAGE_EXECUTABLES lumen "${LUMEN_BUNDLE_NAME}")
set(CPACK_STRIP_FILES TRUE)

if(WIN32)
    set(CPACK_GENERATOR WIX)
    set(CPACK_PACKAGE_FILE_NAME "Lumen-Presenter-${PROJECT_VERSION}-windows-x64")
    # Never change: it lets a newer .msi replace an older one.
    set(CPACK_WIX_UPGRADE_GUID "6F1B0C9E-3D2A-4E57-9B8C-2A4F7E1D5C30")
    set(CPACK_WIX_PRODUCT_ICON ${CMAKE_CURRENT_SOURCE_DIR}/icons/lumen-presenter.ico)
    set(CPACK_WIX_PROGRAM_MENU_FOLDER "${LUMEN_BUNDLE_NAME}")
    set(CPACK_WIX_PROPERTY_ARPURLINFOABOUT "${CPACK_PACKAGE_HOMEPAGE_URL}")
    set(CPACK_WIX_PROPERTY_ARPHELPLINK "${CPACK_PACKAGE_HOMEPAGE_URL}/issues")
    set(CPACK_CREATE_DESKTOP_LINKS lumen)
elseif(APPLE)
    set(CPACK_GENERATOR DragNDrop)
    set(CPACK_PACKAGE_FILE_NAME "Lumen-Presenter-${PROJECT_VERSION}-macos")
    set(CPACK_DMG_VOLUME_NAME "${LUMEN_BUNDLE_NAME}")
    set(CPACK_DMG_FORMAT UDZO)
    # Only the bundle (and the Applications link) in the window.
    set(CPACK_PACKAGE_EXECUTABLES "")
else()
    set(CPACK_GENERATOR TGZ)
    set(CPACK_PACKAGING_INSTALL_PREFIX /usr)
    set(CPACK_PACKAGE_EXECUTABLES "")

    # .deb: library dependencies are worked out by dpkg-shlibdeps; the QML
    # modules and Qt plugins Lumen loads at run time are listed here.
    set(CPACK_DEBIAN_FILE_NAME DEB-DEFAULT)
    set(CPACK_DEBIAN_PACKAGE_SECTION video)
    set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS ON)
    set(CPACK_DEBIAN_PACKAGE_DEPENDS
        "qml6-module-qtquick, qml6-module-qtquick-window, qml6-module-qtquick-layouts, qml6-module-qtquick-dialogs, qml6-module-qtquick-controls, qml6-module-qtquick-templates, qml6-module-qtqml-workerscript, qml6-module-qtmultimedia, qml6-module-qt-labs-folderlistmodel, libqt6sql6-sqlite, qt6-qpa-plugins, qt6-wayland")

    # .rpm: library dependencies come from rpm's automatic requirements; the
    # QML modules are in these packages.
    set(CPACK_RPM_FILE_NAME RPM-DEFAULT)
    set(CPACK_RPM_PACKAGE_LICENSE "GPL-3.0-or-later")
    set(CPACK_RPM_PACKAGE_GROUP "Applications/Multimedia")
    set(CPACK_RPM_PACKAGE_URL "${CPACK_PACKAGE_HOMEPAGE_URL}")
    set(CPACK_RPM_PACKAGE_REQUIRES "qt6-qtdeclarative, qt6-qtmultimedia, qt6-qtwayland")
    set(CPACK_RPM_PACKAGE_AUTOREQ ON)
    set(CPACK_RPM_EXCLUDE_FROM_AUTO_FILELIST_ADDITION
        /usr/share/applications /usr/share/icons /usr/share/icons/hicolor /usr/share/icons/hicolor/scalable
        /usr/share/icons/hicolor/scalable/apps /usr/share/metainfo)
endif()

include(CPack)
