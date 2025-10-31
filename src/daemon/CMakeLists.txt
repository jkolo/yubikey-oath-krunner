# YubiKey OATH D-Bus Daemon

# Generate D-Bus adaptors from XML interfaces
# New hierarchical D-Bus interfaces
qt6_add_dbus_adaptor(DBUS_ADAPTOR_SRCS
    dbus/pl.jkolo.yubikey.oath.Manager.xml
    dbus/yubikey_manager_object.h
    YubiKeyOath::Daemon::YubiKeyManagerObject
)

qt6_add_dbus_adaptor(DBUS_ADAPTOR_SRCS
    dbus/pl.jkolo.yubikey.oath.Device.xml
    dbus/yubikey_device_object.h
    YubiKeyOath::Daemon::YubiKeyDeviceObject
)

qt6_add_dbus_adaptor(DBUS_ADAPTOR_SRCS
    dbus/pl.jkolo.yubikey.oath.Credential.xml
    dbus/yubikey_credential_object.h
    YubiKeyOath::Daemon::YubiKeyCredentialObject
)

# Add Qt resources for icons
set(DAEMON_RESOURCES)
qt6_add_resources(DAEMON_RESOURCES
    ../shared/resources/shared.qrc
)

# Daemon executable
add_executable(yubikey-oath-daemon
    # Main daemon files
    main.cpp
    yubikey_dbus_service.cpp
    ../shared/types/yubikey_value_types.cpp
    ../shared/config/configuration_provider.cpp
    ${DBUS_ADAPTOR_SRCS}
    ${DAEMON_RESOURCES}

    # New D-Bus object hierarchy
    dbus/yubikey_manager_object.cpp
    dbus/yubikey_device_object.cpp
    dbus/yubikey_credential_object.cpp

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
    workflows/notification_helper.cpp
    workflows/notification_utils.cpp

    # UI
    ui/add_credential_dialog.cpp
    ui/processing_overlay.cpp

    # Utils
    utils/screenshot_capturer.cpp
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
find_package(KF6 REQUIRED COMPONENTS Notifications Config GuiAddons WidgetsAddons)
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
    KF6::WidgetsAddons  # Provides KMessageWidget for inline messages
    Plasma::KWaylandClient
    yubikey_dbus_client  # For CredentialFormatter
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
install(FILES pl.jkolo.yubikey.oath.daemon.service
        DESTINATION ${KDE_INSTALL_DBUSSERVICEDIR})

# systemd user service file
install(FILES app-pl.jkolo.yubikey.oath.daemon.service
        DESTINATION ${KDE_INSTALL_SYSTEMDUSERUNITDIR})

# Desktop file for app ID
install(FILES pl.jkolo.yubikey.oath.daemon.desktop
        DESTINATION ${KDE_INSTALL_APPDIR})
