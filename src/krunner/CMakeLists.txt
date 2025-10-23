# KRunner YubiKey Plugin
# Uses D-Bus client to communicate with daemon

set(krunner_yubikey_SRCS
    # Runner entry point
    yubikeyrunner.cpp

    # Configuration components
    config/configuration_provider.cpp
    config/krunner_configuration.cpp

    # Shared utilities
    ../shared/utils/async_waiter.cpp

    # Shared UI components
    ../shared/ui/password_dialog.cpp
    ../shared/ui/password_dialog_helper.cpp
    ../shared/ui/logging_categories.cpp

    # Action components
    actions/action_executor.cpp
    actions/action_manager.cpp

    # Matching components
    matching/match_builder.cpp

    # Workflow components
    workflows/notification_orchestrator.cpp
    workflows/notification_helper.cpp
    workflows/notification_utils.cpp
    workflows/touch_workflow_coordinator.cpp
    workflows/touch_handler.cpp

    # Formatting components
    formatting/credential_formatter.cpp
    formatting/code_validator.cpp

    # Input components (auto-type)
    input/text_input_provider.cpp
    input/x11_text_input.cpp
    input/wayland_text_input.cpp
    input/portal_text_input.cpp
    input/text_input_factory.cpp
    input/modifier_key_checker.cpp

    # Clipboard components
    clipboard/clipboard_manager.cpp

    # Notification components
    notification/dbus_notification_manager.cpp

    # KRunner utilities
    logging_categories.cpp
)

qt6_add_resources(krunner_yubikey_SRCS ../shared/resources/shared.qrc)

# Find X11 for modifier key checking (XQueryKeymap)
find_package(X11 REQUIRED)

# Use add_library MODULE like other KRunner plugins
add_library(krunner_yubikey MODULE ${krunner_yubikey_SRCS})

target_include_directories(krunner_yubikey PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/..
    ${XKBCOMMON_INCLUDE_DIRS}
    ${LIBEI_INCLUDE_DIRS}
    ${LIBOEFFIS_INCLUDE_DIRS}
)

target_link_libraries(krunner_yubikey
    Qt6::Core
    Qt6::Widgets
    Qt6::Gui
    Qt6::DBus
    KF6::Runner
    KF6::I18n
    KF6::ConfigCore
    KF6::ConfigWidgets
    KF6::Notifications
    KF6::CoreAddons
    Plasma::KWaylandClient
    yubikey_dbus_client  # NEW - D-Bus client library
    ${XKBCOMMON_LIBRARIES}
    ${LIBEI_LIBRARIES}
    ${LIBOEFFIS_LIBRARIES}
    ${X11_LIBRARIES}  # X11 for XQueryKeymap modifier detection
)

# Install the plugin module
install(TARGETS krunner_yubikey DESTINATION ${KDE_INSTALL_QTPLUGINDIR}/kf6/krunner/)

# Install desktop file
install(FILES plasma-runner-yubikey.desktop DESTINATION ${KDE_INSTALL_DATADIR}/krunner/dbusplugins/)

# Install configuration schema (shared)
install(FILES ../shared/config/krunner_yubikey.kcfg DESTINATION ${KDE_INSTALL_KCFGDIR})

# Install notification configuration
install(FILES krunner_yubikey.notifyrc DESTINATION ${KDE_INSTALL_KNOTIFYRCDIR})
