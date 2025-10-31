# Configuration module for YubiKey OATH runner
set(kcm_krunner_yubikey_SRCS
    yubikey_config.cpp
    yubikey_device_model.cpp
    logging_categories.cpp

    # Shared UI components
    ../shared/ui/password_dialog.cpp
    ../shared/ui/password_dialog_helper.cpp
    ../shared/ui/change_password_dialog.cpp
    ../shared/ui/change_password_dialog_helper.cpp
    ../shared/ui/logging_categories.cpp
)

# Wrap UI files BEFORE adding resources
ki18n_wrap_ui(kcm_krunner_yubikey_SRCS yubikey_config.ui)

# Add Qt resources for icons (shared) and QML files (config-specific)
qt6_add_resources(kcm_krunner_yubikey_SRCS
    ../shared/resources/shared.qrc
    resources/config.qrc
)

add_library(kcm_krunner_yubikey MODULE ${kcm_krunner_yubikey_SRCS})

target_link_libraries(kcm_krunner_yubikey
    Qt6::Core
    Qt6::Widgets
    Qt6::Qml
    Qt6::Quick
    Qt6::QuickWidgets
    Qt6::DBus
    KF6::I18n
    KF6::ConfigCore
    KF6::CoreAddons
    KF6::KCMUtils
    yubikey_dbus_client
)

# Install the KCModule plugin
install(TARGETS kcm_krunner_yubikey DESTINATION ${KDE_INSTALL_QTPLUGINDIR})