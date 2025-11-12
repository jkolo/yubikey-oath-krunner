# YubiKey OATH D-Bus Daemon

# Generate D-Bus adaptors from XML interfaces
# New hierarchical D-Bus interfaces
qt6_add_dbus_adaptor(DBUS_ADAPTOR_SRCS
    dbus/pl.jkolo.yubikey.oath.Manager.xml
    dbus/oath_manager_object.h
    YubiKeyOath::Daemon::OathManagerObject
)

qt6_add_dbus_adaptor(DBUS_ADAPTOR_SRCS
    dbus/pl.jkolo.yubikey.oath.Device.xml
    dbus/oath_device_object.h
    YubiKeyOath::Daemon::OathDeviceObject
)

qt6_add_dbus_adaptor(DBUS_ADAPTOR_SRCS
    dbus/pl.jkolo.yubikey.oath.Credential.xml
    dbus/oath_credential_object.h
    YubiKeyOath::Daemon::OathCredentialObject
)

# Daemon executable
add_executable(yubikey-oath-daemon
    # Main daemon files
    main.cpp
    yubikey_dbus_service.cpp
    ../shared/types/yubikey_value_types.cpp
    ../shared/types/device_brand.cpp
    ../shared/types/device_capabilities.cpp
    ../shared/utils/credential_finder.cpp
    ../shared/utils/device_name_formatter.cpp
    ../shared/utils/version.cpp
    ../shared/utils/yubikey_icon_resolver.cpp
    ${DBUS_ADAPTOR_SRCS}

    # New D-Bus object hierarchy
    dbus/oath_manager_object.cpp
    dbus/oath_device_object.cpp
    dbus/oath_credential_object.cpp

    # OATH components (PC/SC communication)
    oath/oath_device.cpp
    oath/yubikey_device_manager.cpp
    oath/yubikey_oath_device.cpp
    oath/nitrokey_oath_device.cpp
    oath/oath_protocol.cpp
    oath/yk_oath_protocol.cpp
    oath/nitrokey_secrets_oath_protocol.cpp
    oath/yk_oath_session.cpp
    oath/nitrokey_oath_session.cpp
    oath/management_protocol.cpp
    oath/nitrokey_model_detector.cpp

    # Storage components
    storage/yubikey_database.cpp
    storage/secret_storage.cpp
    storage/transaction_guard.cpp

    # PC/SC monitoring
    pcsc/card_reader_monitor.cpp

    # Configuration
    config/daemon_configuration.cpp

    # Services (business logic layer)
    services/yubikey_service.cpp
    services/password_service.cpp
    services/device_lifecycle_service.cpp
    services/credential_service.cpp

    # Actions
    actions/action_executor.cpp
    actions/yubikey_action_coordinator.cpp

    # Cache
    cache/credential_cache_searcher.cpp

    # Clipboard
    clipboard/clipboard_manager.cpp

    # Input (text input emulation)
    input/text_input_factory.cpp
    input/x11_text_input.cpp
    input/portal_text_input.cpp
    input/modifier_key_checker.cpp

    # Notifications
    notification/dbus_notification_manager.cpp

    # Workflows
    workflows/notification_orchestrator.cpp
    workflows/touch_handler.cpp
    workflows/touch_workflow_coordinator.cpp
    workflows/reconnect_workflow_coordinator.cpp
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
    utils/secure_memory.cpp

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
    PkgConfig::LIBPORTAL_QT6
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
