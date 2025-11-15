# Configuration module for YubiKey OATH runner
set(kcm_krunner_yubikey_SRCS
    oath_config.cpp
    oath_device_list_model.cpp
    device_delegate.cpp
    device_card_layout.cpp
    device_card_painter.cpp
    oath_config_icon_resolver.cpp
    relative_time_formatter.cpp
    logging_categories.cpp

    # Shared UI components
    ../shared/ui/password_dialog.cpp
    ../shared/ui/password_dialog_helper.cpp
    ../shared/ui/change_password_dialog.cpp
    ../shared/ui/change_password_dialog_helper.cpp
    ../shared/ui/logging_categories.cpp

    # Shared utilities
    ../shared/utils/yubikey_icon_resolver.cpp
)

# Wrap UI files
ki18n_wrap_ui(kcm_krunner_yubikey_SRCS oath_config.ui)

add_library(kcm_krunner_yubikey MODULE ${kcm_krunner_yubikey_SRCS})

target_link_libraries(kcm_krunner_yubikey
    Qt6::Core
    Qt6::Widgets
    Qt6::DBus
    KF6::I18n
    KF6::ConfigCore
    KF6::CoreAddons
    KF6::KCMUtils
    KF6::WidgetsAddons
    yubikey_dbus_client
)

# Install the KCModule plugin
install(TARGETS kcm_krunner_yubikey DESTINATION ${KDE_INSTALL_QTPLUGINDIR})