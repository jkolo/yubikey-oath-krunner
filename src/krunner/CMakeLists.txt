# KRunner YubiKey Plugin
# Uses D-Bus client to communicate with daemon

set(krunner_yubikey_SRCS
    # Runner entry point
    oathrunner.cpp

    # Configuration components
    config/krunner_configuration.cpp

    # Shared UI components (password dialogs still in shared, used by config too)
    ../shared/ui/password_dialog.cpp
    ../shared/ui/password_dialog_helper.cpp
    ../shared/ui/logging_categories.cpp

    # Shared utilities
    ../shared/utils/yubikey_icon_resolver.cpp

    # Action components (action_manager only - manages UI metadata)
    actions/action_manager.cpp

    # Matching components
    matching/match_builder.cpp

    # KRunner utilities
    logging_categories.cpp
)

# Use add_library MODULE like other KRunner plugins
add_library(krunner_yubikey MODULE ${krunner_yubikey_SRCS})

target_include_directories(krunner_yubikey PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/..
)

target_link_libraries(krunner_yubikey
    Qt6::Core
    Qt6::Widgets
    Qt6::Gui
    Qt6::DBus
    KF6::Runner
    KF6::I18n
    KF6::ConfigCore
    yubikey_dbus_client  # D-Bus client library for daemon communication
)

# Install the plugin module
install(TARGETS krunner_yubikey DESTINATION ${KDE_INSTALL_QTPLUGINDIR}/kf6/krunner/)

# Install desktop file
install(FILES plasma-runner-yubikey.desktop DESTINATION ${KDE_INSTALL_DATADIR}/krunner/dbusplugins/)

# Install configuration schema (shared)
install(FILES ../shared/config/yubikey_oath.kcfg DESTINATION ${KDE_INSTALL_KCFGDIR})

# Install notification configuration
install(FILES krunner_yubikey.notifyrc DESTINATION ${KDE_INSTALL_KNOTIFYRCDIR})
