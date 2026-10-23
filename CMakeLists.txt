cmake_minimum_required(VERSION 3.24)
project(FrankensteinMacintosh VERSION 0.1.0 LANGUAGES C CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
find_package(Qt6 6.8 REQUIRED COMPONENTS Core Gui Widgets DBus Test Quick Qml WaylandCompositor Concurrent Network)
# Pointer capture needs the host wl_surface of the desktop window and wl_region contents, and
# precise scrolling needs the clients' wl_pointer resources. All are private Qt API, confined to
# src/desktop/host_pointer.cpp, pointer_constraints.cpp and client_surface_item.cpp.
find_package(Qt6 REQUIRED COMPONENTS GuiPrivate WaylandCompositorPrivate)
find_package(PkgConfig REQUIRED)
pkg_check_modules(WAYLAND_SERVER REQUIRED IMPORTED_TARGET wayland-server>=1.26)
pkg_check_modules(WAYLAND_CLIENT REQUIRED IMPORTED_TARGET wayland-client)
pkg_check_modules(GIO REQUIRED IMPORTED_TARGET gio-2.0)
add_library(frankenstein-applications src/applications/desktop_entry.cpp src/applications/desktop_entry.hpp src/applications/application_catalog.cpp src/applications/application_catalog.hpp src/applications/launch_environment.cpp src/applications/launch_environment.hpp)
target_sources(frankenstein-applications PRIVATE src/applications/application_appearance.cpp src/applications/application_appearance.hpp)
target_sources(frankenstein-applications PRIVATE src/applications/emulator_launch.cpp src/applications/emulator_launch.hpp)
qt_add_resources(frankenstein-applications application_appearance PREFIX "/application-appearance" FILES src/appearance/gtk-platinum.css src/appearance/firefox-userChrome.css)
target_include_directories(frankenstein-applications PUBLIC src)
set_source_files_properties(src/applications/desktop_entry.cpp PROPERTIES COMPILE_DEFINITIONS QT_NO_KEYWORDS)
target_link_libraries(frankenstein-applications PUBLIC Qt6::Core Qt6::Concurrent PRIVATE PkgConfig::GIO)
target_compile_options(frankenstein-applications PRIVATE -Wall -Wextra -Wpedantic)
add_library(frankenstein-policy src/policy/transition.cpp)
add_executable(platinum-wallet-service src/desktop/wallet_service_main.cpp)
target_link_libraries(platinum-wallet-service PRIVATE Qt6::Core Qt6::DBus)
target_compile_options(platinum-wallet-service PRIVATE -Wall -Wextra -Wpedantic)
target_include_directories(frankenstein-policy PUBLIC src)
target_compile_options(frankenstein-policy PRIVATE -Wall -Wextra -Wpedantic)
find_program(WAYLAND_SCANNER wayland-scanner REQUIRED)
find_file(APPMENU_PROTOCOL appmenu.xml PATHS /usr/share/qt6/wayland/protocols/appmenu REQUIRED)
add_custom_command(OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/appmenu-server.h COMMAND ${WAYLAND_SCANNER} server-header ${APPMENU_PROTOCOL} ${CMAKE_CURRENT_BINARY_DIR}/appmenu-server.h DEPENDS ${APPMENU_PROTOCOL})
add_custom_command(OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/appmenu-protocol.c COMMAND ${WAYLAND_SCANNER} private-code ${APPMENU_PROTOCOL} ${CMAKE_CURRENT_BINARY_DIR}/appmenu-protocol.c DEPENDS ${APPMENU_PROTOCOL})
pkg_get_variable(WAYLAND_PROTOCOLS_DIR wayland-protocols pkgdatadir)
if(NOT WAYLAND_PROTOCOLS_DIR)
    message(FATAL_ERROR "wayland-protocols is required")
endif()
set(PLATINUM_PROTOCOLS ${CMAKE_CURRENT_BINARY_DIR}/protocols)
file(MAKE_DIRECTORY ${PLATINUM_PROTOCOLS})
function(platinum_wayland_protocol name xml)
    foreach(kind server-header client-header private-code)
        string(REPLACE "-header" ".h" suffix "${kind}")
        string(REPLACE "private-code" "protocol.c" suffix "${suffix}")
        add_custom_command(OUTPUT ${PLATINUM_PROTOCOLS}/${name}-${suffix}
                           COMMAND ${WAYLAND_SCANNER} ${kind} ${xml} ${PLATINUM_PROTOCOLS}/${name}-${suffix} DEPENDS ${xml})
    endforeach()
endfunction()
platinum_wayland_protocol(relative-pointer ${WAYLAND_PROTOCOLS_DIR}/unstable/relative-pointer/relative-pointer-unstable-v1.xml)
platinum_wayland_protocol(pointer-constraints ${WAYLAND_PROTOCOLS_DIR}/unstable/pointer-constraints/pointer-constraints-unstable-v1.xml)
platinum_wayland_protocol(xdg-shell ${WAYLAND_PROTOCOLS_DIR}/stable/xdg-shell/xdg-shell.xml)
platinum_wayland_protocol(idle-inhibit ${WAYLAND_PROTOCOLS_DIR}/unstable/idle-inhibit/idle-inhibit-unstable-v1.xml)
platinum_wayland_protocol(xdg-activation ${WAYLAND_PROTOCOLS_DIR}/staging/xdg-activation/xdg-activation-v1.xml)
platinum_wayland_protocol(xdg-system-bell ${WAYLAND_PROTOCOLS_DIR}/staging/xdg-system-bell/xdg-system-bell-v1.xml)
platinum_wayland_protocol(primary-selection ${WAYLAND_PROTOCOLS_DIR}/unstable/primary-selection/primary-selection-unstable-v1.xml)
platinum_wayland_protocol(ext-data-control ${WAYLAND_PROTOCOLS_DIR}/staging/ext-data-control/ext-data-control-v1.xml)
platinum_wayland_protocol(ext-session-lock ${WAYLAND_PROTOCOLS_DIR}/staging/ext-session-lock/ext-session-lock-v1.xml)
# Test driver only. Copied from wayland-protocols-wlr 0.3.9 (wlr-protocols), SHA-256
# 3ff6d540be0bc5228195bf072bde42117ea17945a5c2061add5d3cf97d6bb524; see docs/relative-pointer-plan.md.
platinum_wayland_protocol(wlr-virtual-pointer ${CMAKE_CURRENT_SOURCE_DIR}/tests/protocols/wlr-virtual-pointer-unstable-v1.xml)
# Test driver only; see the header of the file (checked against libwlroots-0.20).
platinum_wayland_protocol(virtual-keyboard ${CMAKE_CURRENT_SOURCE_DIR}/tests/protocols/virtual-keyboard-unstable-v1.xml)
add_library(frankenstein-desktop src/desktop/manager.cpp src/desktop/manager.hpp src/desktop/desktop_items.cpp src/desktop/desktop_items.hpp src/desktop/client_surface_item.cpp src/desktop/client_surface_item.hpp src/desktop/menu_bridge.cpp src/desktop/menu_bridge.hpp src/desktop/menu_broker.cpp src/desktop/menu_broker.hpp ${CMAKE_CURRENT_BINARY_DIR}/appmenu-server.h ${CMAKE_CURRENT_BINARY_DIR}/appmenu-protocol.c)
target_sources(frankenstein-desktop PRIVATE src/desktop/document_service.cpp src/desktop/document_service.hpp)
target_sources(frankenstein-desktop PRIVATE src/desktop/appearance_settings.cpp src/desktop/appearance_settings.hpp)
target_sources(frankenstein-desktop PRIVATE src/desktop/keyboard_broadcast.cpp src/desktop/keyboard_broadcast.hpp)
target_sources(frankenstein-desktop PRIVATE src/desktop/clipboard.cpp src/desktop/clipboard.hpp)
find_package(Threads REQUIRED)
add_library(platinum-clipboard-transfers src/desktop/clipboard_transfers.cpp src/desktop/clipboard_transfers.hpp)
target_include_directories(platinum-clipboard-transfers PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(platinum-clipboard-transfers PUBLIC Qt6::Core PRIVATE Threads::Threads)
target_compile_options(platinum-clipboard-transfers PRIVATE -Wall -Wextra -Wpedantic)
target_link_libraries(frankenstein-desktop PRIVATE platinum-clipboard-transfers)
target_sources(frankenstein-desktop PRIVATE src/desktop/command_keys.cpp src/desktop/command_keys.hpp)
target_sources(frankenstein-desktop PRIVATE src/desktop/screenshot_service.cpp src/desktop/screenshot_service.hpp)
target_sources(frankenstein-desktop PRIVATE src/desktop/system_controls.cpp src/desktop/system_controls.hpp)
target_sources(frankenstein-desktop PRIVATE src/desktop/output_geometry.cpp src/desktop/output_geometry.hpp)
target_sources(frankenstein-desktop PRIVATE src/desktop/display_layout.cpp src/desktop/display_layout.hpp)
target_sources(frankenstein-desktop PRIVATE src/desktop/popup_placement.cpp src/desktop/popup_placement.hpp)
target_sources(frankenstein-desktop PRIVATE src/desktop/display_topology_feed.cpp src/desktop/display_topology_feed.hpp)
target_sources(frankenstein-desktop PRIVATE src/desktop/dmabuf_formats.cpp src/desktop/graphics_integration.hpp)
target_sources(frankenstein-desktop PRIVATE src/desktop/x11_bridge.cpp src/desktop/x11_bridge.hpp)
target_sources(frankenstein-desktop PRIVATE src/desktop/wallet_activation.cpp src/desktop/wallet_activation.hpp)
target_sources(frankenstein-desktop PRIVATE src/desktop/host_pointer.cpp src/desktop/host_pointer.hpp
               src/desktop/pointer_constraints.cpp src/desktop/pointer_constraints.hpp
               src/desktop/pointer_frames.cpp src/desktop/pointer_frames.hpp
               src/desktop/client_cursors.cpp src/desktop/client_cursors.hpp
               src/desktop/session_control.cpp src/desktop/session_control.hpp
               src/desktop/power_control.cpp src/desktop/power_control.hpp
               src/desktop/removable_media.cpp src/desktop/removable_media.hpp
               src/desktop/icon_store.cpp src/desktop/icon_store.hpp
               src/desktop/notifications.cpp src/desktop/notifications.hpp
               src/desktop/bluetooth.cpp src/desktop/bluetooth.hpp
               src/desktop/wifi.cpp src/desktop/wifi.hpp
               src/desktop/alert_sounds.cpp src/desktop/alert_sounds.hpp
               src/desktop/interface_sounds.cpp src/desktop/interface_sounds.hpp
               src/desktop/battery_status.cpp src/desktop/battery_status.hpp
               src/desktop/status_items.cpp src/desktop/status_items.hpp
               src/desktop/host_idle.cpp src/desktop/host_idle.hpp
               src/desktop/idle_inhibition.cpp src/desktop/idle_inhibition.hpp
               ${PLATINUM_PROTOCOLS}/idle-inhibit-server.h ${PLATINUM_PROTOCOLS}/idle-inhibit-client.h
               ${PLATINUM_PROTOCOLS}/idle-inhibit-protocol.c
               ${PLATINUM_PROTOCOLS}/relative-pointer-server.h ${PLATINUM_PROTOCOLS}/relative-pointer-client.h
               ${PLATINUM_PROTOCOLS}/relative-pointer-protocol.c
               ${PLATINUM_PROTOCOLS}/pointer-constraints-server.h ${PLATINUM_PROTOCOLS}/pointer-constraints-client.h
               ${PLATINUM_PROTOCOLS}/pointer-constraints-protocol.c
               src/desktop/attention.cpp src/desktop/attention.hpp
               ${PLATINUM_PROTOCOLS}/xdg-activation-server.h ${PLATINUM_PROTOCOLS}/xdg-activation-protocol.c
               ${PLATINUM_PROTOCOLS}/xdg-system-bell-server.h ${PLATINUM_PROTOCOLS}/xdg-system-bell-protocol.c
               src/desktop/primary_selection.cpp src/desktop/primary_selection.hpp
               ${PLATINUM_PROTOCOLS}/primary-selection-server.h ${PLATINUM_PROTOCOLS}/primary-selection-protocol.c
               src/desktop/data_control.cpp src/desktop/data_control.hpp
               ${PLATINUM_PROTOCOLS}/ext-data-control-server.h ${PLATINUM_PROTOCOLS}/ext-data-control-protocol.c)
# Explicit user-local, provenance-recorded dependency. Configuration never
# downloads or builds third-party code. See scripts/build-x11-bridge.py.
set(PLATINUM_XWAYLAND_SATELLITE "" CACHE FILEPATH "Pinned, qualified Xwayland bridge executable")
set(PLATINUM_XWAYLAND_PROVENANCE "" CACHE PATH "Directory with source archive, license and provenance for the bridge")
if(PLATINUM_XWAYLAND_SATELLITE)
    if(NOT EXISTS "${PLATINUM_XWAYLAND_SATELLITE}" OR NOT EXISTS "${PLATINUM_XWAYLAND_PROVENANCE}/provenance.json")
        message(FATAL_ERROR "Build the provenance-recorded X11 bridge first")
    endif()
    file(READ "${PLATINUM_XWAYLAND_PROVENANCE}/provenance.json" x11_provenance)
    string(JSON x11_expected_binary GET "${x11_provenance}" patched_binary_sha256)
    string(JSON x11_expected_patch GET "${x11_provenance}" compatibility_patch_sha256)
    file(SHA256 "${PLATINUM_XWAYLAND_SATELLITE}" x11_binary_hash)
    file(SHA256 "${CMAKE_CURRENT_SOURCE_DIR}/packaging/xwayland-satellite-qt-shell-v1.patch" x11_patch_hash)
    if(NOT x11_binary_hash STREQUAL x11_expected_binary OR NOT x11_patch_hash STREQUAL x11_expected_patch)
        message(FATAL_ERROR "X11 helper or compatibility patch differs from its provenance")
    endif()
    configure_file("${PLATINUM_XWAYLAND_SATELLITE}" xwayland-satellite COPYONLY)
    configure_file("${PLATINUM_XWAYLAND_PROVENANCE}/provenance.json" xwayland-satellite-provenance.json COPYONLY)
    target_compile_definitions(frankenstein-desktop PUBLIC PLATINUM_X11_ENABLED=1)
    # Preloaded into the system Xwayland only: never instrumented, even in
    # sanitizer builds, because Xwayland itself is not.
    add_library(platinum-xwayland-implicit-sync MODULE src/desktop/xwayland_implicit_sync.c)
    set_target_properties(platinum-xwayland-implicit-sync PROPERTIES PREFIX "lib" C_VISIBILITY_PRESET hidden AUTOMOC OFF
                          LIBRARY_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/xwayland")
    target_compile_options(platinum-xwayland-implicit-sync PRIVATE -Wall -Wextra -Wpedantic -fno-sanitize=all)
    target_link_options(platinum-xwayland-implicit-sync PRIVATE -fno-sanitize=all -Wl,--no-undefined)
    configure_file(src/desktop/xwayland-launcher.sh xwayland/Xwayland COPYONLY
                   FILE_PERMISSIONS OWNER_READ OWNER_WRITE OWNER_EXECUTE GROUP_READ GROUP_EXECUTE WORLD_READ WORLD_EXECUTE)
    add_dependencies(frankenstein-desktop platinum-xwayland-implicit-sync)
endif()
configure_file(src/desktop/system_controls.py system_controls.py COPYONLY)
configure_file(src/desktop/audio_controls.py audio_controls.py COPYONLY)
target_sources(frankenstein-desktop PRIVATE src/desktop/audio_service.cpp src/desktop/audio_service.hpp)
target_include_directories(frankenstein-desktop PRIVATE ${CMAKE_CURRENT_BINARY_DIR})
target_link_libraries(frankenstein-desktop PUBLIC frankenstein-policy frankenstein-applications Qt6::Quick Qt6::Qml Qt6::DBus Qt6::WaylandCompositor PRIVATE PkgConfig::WAYLAND_SERVER PkgConfig::WAYLAND_CLIENT Qt6::GuiPrivate Qt6::WaylandCompositorPrivate Qt6::Network)
target_include_directories(frankenstein-desktop PRIVATE ${PLATINUM_PROTOCOLS})
target_compile_options(frankenstein-desktop PRIVATE -Wall -Wextra -Wpedantic)
qt_add_resources(frankenstein-desktop desktop-assets PREFIX "/desktop" FILES src/desktop/Desktop.qml src/desktop/ClientSurface.qml src/desktop/DesktopIcon.qml src/desktop/MenuGlyph.qml src/desktop/ShellDialog.qml src/desktop/PlatinumButton.qml src/desktop/TitleBox.qml src/desktop/titlebar/close-active.svg src/desktop/titlebar/close-pressed.svg src/desktop/titlebar/close-inactive.svg src/desktop/titlebar/zoom-active.svg src/desktop/titlebar/zoom-pressed.svg src/desktop/titlebar/zoom-inactive.svg src/desktop/titlebar/shade-active.svg src/desktop/titlebar/shade-pressed.svg src/desktop/titlebar/shade-inactive.svg src/desktop/PlatinumCheckBox.qml src/desktop/ApplicationBrowser.qml src/desktop/NotificationAlerts.qml src/desktop/MonitorsPanel.qml src/desktop/InfoWindow.qml src/desktop/ClipboardWindow.qml)
qt_add_resources(frankenstein-desktop appearance-assets PREFIX "/desktop" FILES src/desktop/WindowRoll.qml src/desktop/AppearanceDialog.qml src/desktop/ControlStrip.qml src/desktop/StripGlyph.qml)
qt_add_resources(frankenstein-desktop picture-assets PREFIX "/desktop" FILES src/desktop/DesktopPictureDialog.qml)
qt_add_resources(frankenstein-desktop screenshot-assets PREFIX "/desktop" FILES src/desktop/ScreenshotDialog.qml)
qt_add_resources(frankenstein-desktop controls-assets PREFIX "/desktop" FILES src/desktop/QuickControls.qml)
qt_add_resources(frankenstein-desktop bluetooth-assets PREFIX "/desktop" FILES src/desktop/BluetoothPanel.qml src/desktop/BluetoothAlert.qml src/desktop/WifiAlert.qml)
qt_add_resources(frankenstein-desktop sound-assets PREFIX "/desktop" FILES src/desktop/SoundPanel.qml)
add_executable(frankenstein src/desktop/main.cpp)
target_link_libraries(frankenstein PRIVATE frankenstein-desktop)
# dmabuf_formats.cpp interposes the Qt dmabuf plugin's EGL lookup from here.
target_link_options(frankenstein PRIVATE "LINKER:--export-dynamic-symbol=eglGetProcAddress")
add_executable(frankenstein-drop-receiver src/desktop/drop_receiver.cpp src/desktop/drop_receiver_window.cpp src/desktop/drop_receiver_window.hpp)
target_include_directories(frankenstein-drop-receiver PRIVATE src)
target_link_libraries(frankenstein-drop-receiver PRIVATE Qt6::Gui Qt6::DBus)
add_library(frankenstein-files src/finder/file_operations.cpp src/finder/file_operations.hpp src/finder/file_details.cpp src/finder/file_details.hpp)
target_include_directories(frankenstein-files PUBLIC src)
# GIO has a struct member named signals; keep Qt's keyword macros out of this
# backend instead of relying on a fragile include order.
target_compile_definitions(frankenstein-files PRIVATE QT_NO_KEYWORDS)
target_link_libraries(frankenstein-files PUBLIC Qt6::Core PRIVATE PkgConfig::GIO)
target_link_libraries(frankenstein-desktop PRIVATE frankenstein-files Qt6::Concurrent)
add_library(frankenstein-appearance STATIC src/finder/platinum_style.cpp src/finder/platinum_style.hpp)
set_target_properties(frankenstein-appearance PROPERTIES POSITION_INDEPENDENT_CODE ON)
target_include_directories(frankenstein-appearance PUBLIC src)
target_link_libraries(frankenstein-appearance PUBLIC Qt6::Widgets)
add_library(frankenstein-style MODULE src/appearance/style_plugin.cpp src/appearance/style.json)
set_target_properties(frankenstein-style PROPERTIES LIBRARY_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/plugins/styles")
target_link_libraries(frankenstein-style PRIVATE frankenstein-appearance)
add_library(frankenstein-finder-ui src/finder/finder_window.cpp src/finder/finder_window.hpp src/finder/finder_service.cpp src/finder/finder_service.hpp src/editor/editor_window.cpp src/editor/editor_window.hpp)
target_sources(frankenstein-finder-ui PRIVATE src/finder/window_surface_id.cpp src/finder/window_surface_id.hpp)
target_sources(frankenstein-finder-ui PRIVATE src/finder/clipboard_window.cpp src/finder/clipboard_window.hpp)
target_link_libraries(frankenstein-finder-ui PRIVATE PkgConfig::WAYLAND_CLIENT)
target_link_libraries(frankenstein-finder-ui PUBLIC frankenstein-files frankenstein-appearance Qt6::Widgets Qt6::Concurrent Qt6::DBus)
add_executable(frankenstein-finder src/finder/main.cpp)
target_link_libraries(frankenstein-finder PRIVATE frankenstein-finder-ui Qt6::DBus)
add_executable(frankenstein-editor src/editor/main.cpp)
target_link_libraries(frankenstein-editor PRIVATE frankenstein-finder-ui)
add_library(frankenstein-puzzle-ui src/puzzle/sliding_puzzle.cpp src/puzzle/puzzle_window.cpp src/puzzle/puzzle_window.hpp)
target_include_directories(frankenstein-puzzle-ui PUBLIC src)
target_link_libraries(frankenstein-puzzle-ui PUBLIC frankenstein-appearance Qt6::Widgets)
target_compile_options(frankenstein-puzzle-ui PRIVATE -Wall -Wextra -Wpedantic)
add_executable(frankenstein-puzzle src/puzzle/main.cpp)
target_link_libraries(frankenstein-puzzle PRIVATE frankenstein-puzzle-ui)
add_dependencies(frankenstein frankenstein-puzzle)
add_dependencies(frankenstein frankenstein-finder frankenstein-editor frankenstein-style frankenstein-drop-receiver)

# The Platinum screen lock (Mac OS 9.2.2 Log In style) for the session supervisor. PAM decides
# passwords; the lock role itself is Qt private API, confined to src/lock/session_lock.cpp.
find_package(Qt6 REQUIRED COMPONENTS WaylandClient WaylandClientPrivate)
pkg_check_modules(XKBCOMMON QUIET IMPORTED_TARGET xkbcommon)
find_library(PAM_LIBRARY pam REQUIRED)
add_library(platinum-lock-ui STATIC src/lock/authenticator.cpp src/lock/authenticator.hpp
            src/lock/fingerprint.cpp src/lock/fingerprint.hpp src/lock/lock_controller.cpp src/lock/lock_controller.hpp)
qt_add_resources(platinum-lock-ui lock-assets PREFIX "/lock" FILES src/lock/LockScreen.qml)
target_link_libraries(platinum-lock-ui PUBLIC Qt6::Quick Qt6::DBus ${PAM_LIBRARY})
target_compile_options(platinum-lock-ui PRIVATE -Wall -Wextra -Wpedantic)
add_executable(platinum-lock src/lock/main.cpp src/lock/session_lock.cpp src/lock/session_lock.hpp
               ${PLATINUM_PROTOCOLS}/ext-session-lock-client.h ${PLATINUM_PROTOCOLS}/ext-session-lock-protocol.c)
target_include_directories(platinum-lock PRIVATE ${PLATINUM_PROTOCOLS})
target_link_libraries(platinum-lock PRIVATE platinum-lock-ui Qt6::WaylandClient Qt6::WaylandClientPrivate
                      PkgConfig::WAYLAND_CLIENT)
target_compile_options(platinum-lock PRIVATE -Wall -Wextra)
add_dependencies(frankenstein platinum-lock)

# Keep helpers and the ABI-matched style plugin together. No system Qt plugin,
# login-session, autostart, service, or user-profile files are installed.
include(GNUInstallDirs)
# The documentation folder takes the product name, not the CMake project name.
set(CMAKE_INSTALL_DOCDIR "${CMAKE_INSTALL_DATAROOTDIR}/doc/platinum-reality")
set(PLATINUM_LIBDIR "${CMAKE_INSTALL_LIBDIR}/platinum-reality")
file(RELATIVE_PATH PLATINUM_LIB_FROM_BIN "${CMAKE_INSTALL_FULL_BINDIR}"
     "${CMAKE_INSTALL_PREFIX}/${PLATINUM_LIBDIR}")
configure_file(packaging/platinum-reality.in platinum-reality @ONLY)
configure_file(packaging/platinum-reality-session.in platinum-reality-session @ONLY)
install(TARGETS frankenstein frankenstein-finder frankenstein-editor frankenstein-drop-receiver frankenstein-puzzle
                platinum-lock
        RUNTIME DESTINATION "${PLATINUM_LIBDIR}")
install(TARGETS frankenstein-style LIBRARY DESTINATION "${PLATINUM_LIBDIR}/plugins/styles")
install(PROGRAMS "${CMAKE_CURRENT_BINARY_DIR}/platinum-reality"
        DESTINATION "${CMAKE_INSTALL_BINDIR}")
install(FILES scripts/session_support.py DESTINATION "${PLATINUM_LIBDIR}")
# The whole session, from a login screen: the supervisor and its entry in wayland-sessions.
install(PROGRAMS "${CMAKE_CURRENT_BINARY_DIR}/platinum-reality-session" DESTINATION "${CMAKE_INSTALL_BINDIR}")
install(FILES scripts/standalone-session.py DESTINATION "${PLATINUM_LIBDIR}")
install(FILES packaging/platinum-reality-session.desktop DESTINATION "${CMAKE_INSTALL_DATADIR}/wayland-sessions"
        RENAME platinum-reality.desktop)
install(TARGETS platinum-wallet-service RUNTIME DESTINATION "${PLATINUM_LIBDIR}")
install(FILES src/desktop/system_controls.py src/desktop/audio_controls.py DESTINATION "${PLATINUM_LIBDIR}")
if(PLATINUM_XWAYLAND_SATELLITE)
    install(PROGRAMS "${CMAKE_CURRENT_BINARY_DIR}/xwayland-satellite" DESTINATION "${PLATINUM_LIBDIR}")
    install(FILES "${CMAKE_CURRENT_BINARY_DIR}/xwayland-satellite-provenance.json" DESTINATION "${PLATINUM_LIBDIR}")
    install(TARGETS platinum-xwayland-implicit-sync LIBRARY DESTINATION "${PLATINUM_LIBDIR}/xwayland")
    install(PROGRAMS "${CMAKE_CURRENT_BINARY_DIR}/xwayland/Xwayland" DESTINATION "${PLATINUM_LIBDIR}/xwayland")
    install(FILES "${PLATINUM_XWAYLAND_PROVENANCE}/LICENSE" DESTINATION "${CMAKE_INSTALL_DATADIR}/licenses/xwayland-satellite")
    # Preserve exact corresponding source and local modifications with the local
    # payload; no runtime download or embedded third-party font data.
    install(FILES "${PLATINUM_XWAYLAND_PROVENANCE}/source.tar.gz" "${PLATINUM_XWAYLAND_PROVENANCE}/Cargo.lock"
                  packaging/xwayland-satellite-qt-shell-v1.patch scripts/build-x11-bridge.py
            DESTINATION "${CMAKE_INSTALL_DOCDIR}/xwayland-satellite")
endif()
install(FILES packaging/platinum-reality.desktop DESTINATION "${CMAKE_INSTALL_DATADIR}/applications")
install(FILES packaging/platinum-reality.svg DESTINATION "${CMAKE_INSTALL_DATADIR}/icons/hicolor/scalable/apps")
install(FILES docs/install.md docs/first-session.md docs/x11-integration.md DESTINATION "${CMAKE_INSTALL_DOCDIR}")
install(FILES LICENSE packaging/NOTICE
        DESTINATION "${CMAKE_INSTALL_DATADIR}/licenses/platinum-reality")
include(CTest)
if(BUILD_TESTING)
    # Installed GTK toolkits are optional qualification dependencies, not runtime
    # dependencies of the shell. No package is installed by this build.
    pkg_check_modules(GTK3 QUIET IMPORTED_TARGET gtk+-3.0)
    pkg_check_modules(GTK4 QUIET IMPORTED_TARGET gtk4)
    foreach(gtk_version 3 4)
        if(GTK${gtk_version}_FOUND)
            add_executable(gtk-css-tests${gtk_version} tests/gtk_css_tests.c)
            target_link_libraries(gtk-css-tests${gtk_version} PRIVATE PkgConfig::GTK${gtk_version})
            target_compile_definitions(gtk-css-tests${gtk_version} PRIVATE PLATINUM_CSS_FILE="${CMAKE_CURRENT_SOURCE_DIR}/src/appearance/gtk-platinum.css")
            add_test(NAME gtk${gtk_version}-css COMMAND gtk-css-tests${gtk_version})
            add_executable(gtk-appearance-probe${gtk_version} tests/gtk_appearance_probe.c)
            target_link_libraries(gtk-appearance-probe${gtk_version} PRIVATE PkgConfig::GTK${gtk_version})
        endif()
    endforeach()
    add_executable(application-probe tests/application_probe.cpp)
    add_executable(puzzle-tests tests/puzzle_tests.cpp)
    target_link_libraries(puzzle-tests PRIVATE frankenstein-puzzle-ui Qt6::Test)
    add_test(NAME puzzle COMMAND puzzle-tests)
    set_tests_properties(puzzle PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QPA_PLATFORMTHEME=generic")
    target_link_libraries(application-probe PRIVATE Qt6::Core)
    add_executable(application-tests tests/application_tests.cpp)
    target_link_libraries(application-tests PRIVATE frankenstein-applications Qt6::Test)
    add_dependencies(application-tests application-probe frankenstein-style frankenstein-editor frankenstein-finder)
    add_test(NAME applications COMMAND application-tests)
    add_executable(style-plugin-tests tests/style_plugin_tests.cpp)
    target_include_directories(style-plugin-tests PRIVATE src)
    target_link_libraries(style-plugin-tests PRIVATE Qt6::Widgets Qt6::Test)
    add_dependencies(style-plugin-tests frankenstein-style)
    add_test(NAME style-plugin COMMAND style-plugin-tests)
    set_tests_properties(style-plugin PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QPA_PLATFORMTHEME=generic;QT_PLUGIN_PATH=${CMAKE_CURRENT_BINARY_DIR}/plugins;QT_STYLE_OVERRIDE=Frankenstein")
    find_package(Qt6GuiPrivate REQUIRED)
    find_package(Python3 REQUIRED COMPONENTS Interpreter)
    add_test(NAME session-safety COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/session_support_tests.py)
    add_test(NAME fresh-arch-setup COMMAND ${Python3_EXECUTABLE} -B ${CMAKE_CURRENT_SOURCE_DIR}/tests/fresh_arch_setup_tests.py)
    add_test(NAME wallet-support COMMAND ${Python3_EXECUTABLE} -B ${CMAKE_CURRENT_SOURCE_DIR}/tests/wallet_support_tests.py)
    set_tests_properties(wallet-support PROPERTIES ENVIRONMENT "FRANKENSTEIN_BUILD_DIR=${CMAKE_CURRENT_BINARY_DIR}")
    add_executable(wallet-activation-tests tests/wallet_activation_tests.cpp src/desktop/wallet_activation.cpp)
    target_include_directories(wallet-activation-tests PRIVATE src)
    target_link_libraries(wallet-activation-tests PRIVATE Qt6::Core Qt6::DBus Qt6::Test)
    add_test(NAME wallet-activation COMMAND wallet-activation-tests)
    add_test(NAME output-selection COMMAND ${Python3_EXECUTABLE} -B ${CMAKE_CURRENT_SOURCE_DIR}/tests/output_selection_tests.py)
    add_test(NAME boot-splash COMMAND ${Python3_EXECUTABLE} -B ${CMAKE_CURRENT_SOURCE_DIR}/tests/boot_splash_tests.py)
    add_test(NAME session-entry COMMAND ${Python3_EXECUTABLE} -B ${CMAKE_CURRENT_SOURCE_DIR}/tests/session_entry_tests.py ${CMAKE_CURRENT_BINARY_DIR}/platinum-reality-session)
    add_test(NAME macos9-import COMMAND ${Python3_EXECUTABLE} -B ${CMAKE_CURRENT_SOURCE_DIR}/tests/macos9_import_tests.py)
    add_test(NAME login-switch COMMAND ${Python3_EXECUTABLE} -B ${CMAKE_CURRENT_SOURCE_DIR}/tests/login_switch_tests.py)
    add_executable(display-layout-tests tests/display_layout_tests.cpp src/desktop/display_layout.cpp)
    target_include_directories(display-layout-tests PRIVATE src)
    target_link_libraries(display-layout-tests PRIVATE Qt6::Core Qt6::Test)
    add_test(NAME display-layout COMMAND display-layout-tests)
    add_executable(popup-placement-tests tests/popup_placement_tests.cpp src/desktop/popup_placement.cpp)
    target_include_directories(popup-placement-tests PRIVATE src)
    target_link_libraries(popup-placement-tests PRIVATE Qt6::Core Qt6::Test)
    add_test(NAME popup-placement COMMAND popup-placement-tests)
    add_executable(display-topology-feed-tests tests/display_topology_feed_tests.cpp src/desktop/display_layout.cpp
                   src/desktop/display_topology_feed.cpp src/desktop/display_topology_feed.hpp)
    target_include_directories(display-topology-feed-tests PRIVATE src)
    target_link_libraries(display-topology-feed-tests PRIVATE Qt6::Core Qt6::Test)
    add_test(NAME display-topology-feed COMMAND display-topology-feed-tests)
    add_test(NAME display-snapshot COMMAND ${Python3_EXECUTABLE} -B ${CMAKE_CURRENT_SOURCE_DIR}/tests/display_snapshot_tests.py)
    add_test(NAME system-controls COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/system_controls_tests.py)
    add_test(NAME audio-controls COMMAND ${Python3_EXECUTABLE} -B ${CMAKE_CURRENT_SOURCE_DIR}/tests/audio_controls_tests.py)
    add_executable(audio-service-tests tests/audio_service_tests.cpp)
    target_link_libraries(audio-service-tests PRIVATE frankenstein-desktop Qt6::Test)
    target_compile_definitions(audio-service-tests PRIVATE
        AUDIO_PREVIEW_FILE="${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/audio_preview.qml"
        AUDIO_ARTIFACT_DIRECTORY="${CMAKE_CURRENT_BINARY_DIR}/artifacts/audio-service")
    add_test(NAME audio-service COMMAND audio-service-tests)
    add_executable(sound-integration-tests tests/sound_integration_tests.cpp)
    target_link_libraries(sound-integration-tests PRIVATE frankenstein-desktop Qt6::Test)
    add_test(NAME sound-integration COMMAND sound-integration-tests)
    set_tests_properties(audio-service sound-integration PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software;QT_QPA_PLATFORMTHEME=generic;DBUS_SYSTEM_BUS_ADDRESS=unix:path=/tmp/no-sound-integration-system-bus")
    set_tests_properties(system-controls PROPERTIES ENVIRONMENT "PYTHONDONTWRITEBYTECODE=1")
    set_tests_properties(session-safety PROPERTIES ENVIRONMENT "PYTHONDONTWRITEBYTECODE=1")
    add_executable(lock-tests tests/lock_tests.cpp)
    target_link_libraries(lock-tests PRIVATE platinum-lock-ui Qt6::Test)
    add_test(NAME lock COMMAND lock-tests)
    set_tests_properties(lock PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software")
    # The real lock on an owned headless Sway (two outputs, no input devices, private runtime).
    add_executable(virtual-keyboard-driver tests/virtual_keyboard_driver.c
                   ${PLATINUM_PROTOCOLS}/virtual-keyboard-client.h ${PLATINUM_PROTOCOLS}/virtual-keyboard-protocol.c)
    set_target_properties(virtual-keyboard-driver PROPERTIES AUTOMOC OFF)
    target_include_directories(virtual-keyboard-driver PRIVATE ${PLATINUM_PROTOCOLS})
    target_link_libraries(virtual-keyboard-driver PRIVATE PkgConfig::WAYLAND_CLIENT PkgConfig::XKBCOMMON)
    target_compile_options(virtual-keyboard-driver PRIVATE -Wall -Wextra -Wpedantic)
    add_test(NAME lock-session COMMAND ${Python3_EXECUTABLE} -B ${CMAKE_CURRENT_SOURCE_DIR}/tests/lock_session_tests.py)
    set_tests_properties(lock-session PROPERTIES ENVIRONMENT "FRANKENSTEIN_BUILD_DIR=${CMAKE_CURRENT_BINARY_DIR}"
                         TIMEOUT 120)
    add_executable(policy-tests tests/policy_tests.cpp)
    target_link_libraries(policy-tests PRIVATE frankenstein-policy Qt6::Test)
    add_test(NAME policy COMMAND policy-tests)
    add_executable(file-tests tests/file_tests.cpp)
    target_link_libraries(file-tests PRIVATE frankenstein-files Qt6::Test)
    add_test(NAME files COMMAND file-tests)
    add_executable(gui-tests tests/gui_tests.cpp)
    target_link_libraries(gui-tests PRIVATE frankenstein-finder-ui Qt6::Test)
    add_test(NAME gui COMMAND gui-tests)
    set_tests_properties(gui PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QPA_PLATFORMTHEME=generic")
    add_executable(menu-tests tests/menu_tests.cpp)
    add_executable(clipboard-transfer-tests tests/clipboard_transfer_tests.cpp)
    target_link_libraries(clipboard-transfer-tests PRIVATE platinum-clipboard-transfers Qt6::Test Threads::Threads)
    target_link_options(clipboard-transfer-tests PRIVATE -Wl,--wrap=pthread_create -Wl,--wrap=write)
    add_test(NAME clipboard-transfers COMMAND clipboard-transfer-tests)
    set_tests_properties(clipboard-transfers PROPERTIES TIMEOUT 15)
    target_link_libraries(menu-tests PRIVATE frankenstein-desktop Qt6::Test)
    add_test(NAME menus COMMAND menu-tests)
    add_executable(desktop-tests tests/desktop_tests.cpp src/desktop/drop_receiver_window.cpp src/desktop/drop_receiver_window.hpp)
    target_link_libraries(desktop-tests PRIVATE frankenstein-desktop Qt6::Test Qt6::GuiPrivate Qt6::Network)
    add_test(NAME desktop COMMAND desktop-tests)
    set_tests_properties(desktop PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software")
    add_executable(bluetooth-tests tests/bluetooth_tests.cpp tests/fake_bluez.hpp)
    target_link_libraries(bluetooth-tests PRIVATE frankenstein-desktop Qt6::Test)
    add_test(NAME bluetooth COMMAND bluetooth-tests)
    set_tests_properties(bluetooth PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software")
    add_executable(removable-media-tests tests/removable_media_tests.cpp)
    target_link_libraries(removable-media-tests PRIVATE frankenstein-desktop Qt6::Test)
    add_test(NAME removable-media COMMAND removable-media-tests)
    add_executable(power-tests tests/power_tests.cpp)
    target_link_libraries(power-tests PRIVATE frankenstein-desktop Qt6::Test)
    add_test(NAME power COMMAND power-tests)
    add_executable(wifi-tests tests/wifi_tests.cpp tests/fake_iwd.hpp)
    target_link_libraries(wifi-tests PRIVATE frankenstein-desktop Qt6::Test)
    add_test(NAME wifi COMMAND wifi-tests)
    set_tests_properties(wifi PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software")
    add_executable(desktop-items-tests tests/desktop_items_tests.cpp)
    target_link_libraries(desktop-items-tests PRIVATE frankenstein-desktop Qt6::Test)
    add_test(NAME desktop-items COMMAND desktop-items-tests)
    # Explicit separate venue: these need a private bus, bindable Unix sockets
    # and a working nested graphics path. Never count them as offscreen passes.
    add_executable(native-probe tests/native_probe.cpp)
    target_link_libraries(native-probe PRIVATE Qt6::Widgets Qt6::DBus)
    find_package(Vulkan REQUIRED)
    add_executable(vulkan-probe tests/vulkan_probe.cpp)
    target_link_libraries(vulkan-probe PRIVATE Qt6::Gui Qt6::DBus Vulkan::Headers)
    pkg_check_modules(XCB_DRI3 QUIET IMPORTED_TARGET xcb xcb-dri3)
    if(XCB_DRI3_FOUND)
        target_compile_definitions(vulkan-probe PRIVATE FRANKENSTEIN_PROBE_DRI3=1)
        target_link_libraries(vulkan-probe PRIVATE PkgConfig::XCB_DRI3)
    endif()
    # A raw Wayland game-like client and a host virtual-pointer driver for pointer capture.
    add_executable(pointer-probe tests/pointer_probe.c
                   ${PLATINUM_PROTOCOLS}/xdg-shell-client.h ${PLATINUM_PROTOCOLS}/xdg-shell-protocol.c
                   ${PLATINUM_PROTOCOLS}/relative-pointer-client.h ${PLATINUM_PROTOCOLS}/relative-pointer-protocol.c
                   ${PLATINUM_PROTOCOLS}/pointer-constraints-client.h ${PLATINUM_PROTOCOLS}/pointer-constraints-protocol.c
                   ${PLATINUM_PROTOCOLS}/idle-inhibit-client.h ${PLATINUM_PROTOCOLS}/idle-inhibit-protocol.c)
    add_executable(paste-probe tests/paste_probe.c
                   ${PLATINUM_PROTOCOLS}/xdg-shell-client.h ${PLATINUM_PROTOCOLS}/xdg-shell-protocol.c)
    add_executable(virtual-pointer-driver tests/virtual_pointer_driver.c
                   ${PLATINUM_PROTOCOLS}/wlr-virtual-pointer-client.h ${PLATINUM_PROTOCOLS}/wlr-virtual-pointer-protocol.c)
    pkg_check_modules(SDL3 REQUIRED IMPORTED_TARGET sdl3)
    add_executable(relative-mouse-probe tests/relative_mouse_probe.c)
    add_executable(output-race-probe tests/output_race_probe.c)
    add_executable(activation-probe tests/activation_probe.c
                   ${PLATINUM_PROTOCOLS}/xdg-shell-client.h ${PLATINUM_PROTOCOLS}/xdg-shell-protocol.c
                   ${PLATINUM_PROTOCOLS}/xdg-activation-client.h ${PLATINUM_PROTOCOLS}/xdg-activation-protocol.c)
    target_link_libraries(relative-mouse-probe PRIVATE PkgConfig::SDL3)
    foreach(probe pointer-probe paste-probe virtual-pointer-driver relative-mouse-probe output-race-probe activation-probe)
        set_target_properties(${probe} PROPERTIES AUTOMOC OFF)
        target_include_directories(${probe} PRIVATE ${PLATINUM_PROTOCOLS})
        target_link_libraries(${probe} PRIVATE PkgConfig::WAYLAND_CLIENT)
        target_compile_options(${probe} PRIVATE -Wall -Wextra -Wpedantic)
    endforeach()
    # Test-only and opt-in (FRANKENSTEIN_FIXTURE_PTRACER): lets gdb attach to a hung fixture
    # browser under Yama. Preloaded into uninstrumented browsers, so never sanitized.
    add_library(fixture-ptracer MODULE tests/fixture_ptracer.c)
    set_target_properties(fixture-ptracer PROPERTIES AUTOMOC OFF PREFIX "lib")
    target_compile_options(fixture-ptracer PRIVATE -Wall -Wextra -Wpedantic -fno-sanitize=all)
    target_link_options(fixture-ptracer PRIVATE -fno-sanitize=all)
    add_executable(native-tests tests/native_tests.cpp)
    # Opt-in topology experiments; never silently add a two-monitor requirement
    # to the established single-monitor qualification gate.
    add_executable(multi-display-tests EXCLUDE_FROM_ALL tests/native_tests.cpp)
    # Mirror prototypes are compiled only for the opt-in experiment, never the runtime.
    target_sources(multi-display-tests PRIVATE src/desktop/mirror_cursor.cpp src/desktop/mirror_cursor.hpp
        src/desktop/mirror_touch.cpp src/desktop/mirror_touch.hpp)
    target_compile_definitions(multi-display-tests PRIVATE PLATINUM_MULTI_DISPLAY_EXPERIMENT=1)
    target_link_libraries(multi-display-tests PRIVATE frankenstein-desktop Qt6::Test Qt6::GuiPrivate Qt6::Network)
    target_link_libraries(multi-display-tests PRIVATE PkgConfig::WAYLAND_CLIENT)
    target_link_options(multi-display-tests PRIVATE "LINKER:--export-dynamic-symbol=eglGetProcAddress")
    add_dependencies(multi-display-tests native-probe pointer-probe virtual-pointer-driver output-race-probe)
    # Native input injection must pass through Qt's window-system handler so
    # XKB modifier bookkeeping matches physical events. Private API is test-only.
    target_link_libraries(native-tests PRIVATE frankenstein-desktop Qt6::Test Qt6::GuiPrivate Qt6::Network)
    target_link_libraries(native-tests PRIVATE PkgConfig::WAYLAND_CLIENT)
    target_link_options(native-tests PRIVATE "LINKER:--export-dynamic-symbol=eglGetProcAddress")
    add_dependencies(native-tests native-probe frankenstein-finder frankenstein-editor frankenstein-style frankenstein-drop-receiver)
    add_dependencies(native-tests frankenstein-puzzle)
    add_dependencies(native-tests vulkan-probe pointer-probe paste-probe virtual-pointer-driver relative-mouse-probe fixture-ptracer activation-probe)
    if(GTK3_FOUND AND GTK4_FOUND)
        target_compile_definitions(native-tests PRIVATE FRANKENSTEIN_GTK_PROBES=1)
        add_dependencies(native-tests gtk-appearance-probe3 gtk-appearance-probe4)
    endif()
endif()
