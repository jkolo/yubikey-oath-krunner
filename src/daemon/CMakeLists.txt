# YubiKey OATH D-Bus Daemon

# Generate D-Bus adaptor from XML interface
qt6_add_dbus_adaptor(DBUS_ADAPTOR_SRCS
    org.kde.plasma.krunner.yubikey.xml
    yubikey_dbus_service.h
    KRunner::YubiKey::YubiKeyDBusService
)

# Daemon executable
add_executable(yubikey-oath-daemon
    # Main daemon files
    main.cpp
    yubikey_dbus_service.cpp
    ../shared/dbus/yubikey_dbus_types.cpp
    ../shared/config/configuration_provider.cpp
    ${DBUS_ADAPTOR_SRCS}

    # OATH components (PC/SC communication)
    oath/yubikey_device_manager.cpp
    oath/yubikey_oath_device.cpp
    oath/oath_protocol.cpp
    oath/oath_session.cpp

    # Storage components
    storage/yubikey_database.cpp
    storage/password_storage.cpp

    # PC/SC monitoring
    pcsc/card_reader_monitor.cpp

    # Configuration
    config/daemon_configuration.cpp

    # Services (business logic layer)
    services/yubikey_service.cpp

    # Actions
    actions/action_executor.cpp
    actions/yubikey_action_coordinator.cpp

    # Clipboard
    clipboard/clipboard_manager.cpp

    # Input (text input emulation)
    input/text_input_factory.cpp
    input/text_input_provider.cpp
    input/x11_text_input.cpp
    input/wayland_text_input.cpp
    input/portal_text_input.cpp
    input/modifier_key_checker.cpp

    # Notifications
    notification/dbus_notification_manager.cpp

    # Workflows
    workflows/notification_orchestrator.cpp
    workflows/touch_handler.cpp
    workflows/touch_workflow_coordinator.cpp
    workflows/add_credential_workflow.cpp
    workflows/notification_helper.cpp
    workflows/notification_utils.cpp

    # UI
    ui/add_credential_dialog.cpp

    # Utils
    utils/screenshot_capture.cpp
    utils/qr_code_parser.cpp
    utils/otpauth_uri_parser.cpp
    utils/async_waiter.cpp

    # Formatting
    formatting/code_validator.cpp

    # Daemon utilities
    logging_categories.cpp
)

# Include directories
target_include_directories(yubikey-oath-daemon PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/..
    ${PCSCLITE_INCLUDE_DIRS}
)

# Find required packages for daemon features
find_package(Qt6 REQUIRED COMPONENTS Gui Widgets Qml Quick)
find_package(KF6 REQUIRED COMPONENTS Notifications Config GuiAddons)
find_package(KWayland REQUIRED)
find_package(PkgConfig REQUIRED)
pkg_check_modules(ZXing REQUIRED IMPORTED_TARGET zxing)
pkg_check_modules(X11 REQUIRED IMPORTED_TARGET x11)
pkg_check_modules(XTST REQUIRED IMPORTED_TARGET xtst)
pkg_check_modules(XKBCOMMON REQUIRED IMPORTED_TARGET xkbcommon)
pkg_check_modules(EI REQUIRED IMPORTED_TARGET libei-1.0)
pkg_check_modules(OEFFIS REQUIRED IMPORTED_TARGET liboeffis-1.0)

# Link libraries
target_link_libraries(yubikey-oath-daemon
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
    Qt6::Qml
    Qt6::Quick
    Qt6::DBus
    Qt6::Sql
    Qt6::Concurrent
    KF6::I18n
    KF6::Wallet
    KF6::Notifications
    KF6::ConfigCore  # Provides KSharedConfig
    KF6::GuiAddons   # Provides KSystemClipboard for Wayland
    Plasma::KWaylandClient
    yubikey_dbus_client  # D-Bus client library for workflows
    ${PCSCLITE_LIBRARIES}
    PkgConfig::ZXing
    PkgConfig::X11
    PkgConfig::XTST
    PkgConfig::XKBCOMMON
    PkgConfig::EI
    PkgConfig::OEFFIS
)

# Compiler options for PC/SC
# Note: Use target_compile_options instead of target_compile_definitions
# because PCSCLITE_CFLAGS_OTHER contains compiler flags like -pthread, not defines
target_compile_options(yubikey-oath-daemon PRIVATE
    ${PCSCLITE_CFLAGS_OTHER}
)

# Enable exceptions for QR code parser (required by ZXing)
set_source_files_properties(
    utils/qr_code_parser.cpp
    PROPERTIES COMPILE_FLAGS "-fexceptions"
)

# Installation
install(TARGETS yubikey-oath-daemon
        DESTINATION ${KDE_INSTALL_BINDIR})

# D-Bus service activation file
install(FILES org.kde.plasma.krunner.yubikey.service
        DESTINATION ${KDE_INSTALL_DBUSSERVICEDIR})

# systemd user service file
install(FILES plasma-yubikey-oath-daemon.service
        DESTINATION ${KDE_INSTALL_SYSTEMDUSERUNITDIR})

# Desktop file for app ID
install(FILES org.kde.plasma.krunner.yubikey.desktop
        DESTINATION ${KDE_INSTALL_APPDIR})
