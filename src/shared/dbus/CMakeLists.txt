# YubiKey D-Bus Client Library
# Shared between KRunner plugin and Config module
# Uses hierarchical proxy architecture (Manager → Device → Credential)

# Static library for D-Bus client (shared by krunner and config)
add_library(yubikey_dbus_client STATIC
    yubikey_credential_proxy.cpp
    yubikey_device_proxy.cpp
    yubikey_manager_proxy.cpp
    ../types/yubikey_value_types.cpp
    ../types/yubikey_model.cpp
    ../utils/version.cpp
    ../formatting/credential_formatter.cpp
)

# Enable PIC for static library to allow linking with shared libraries
set_target_properties(yubikey_dbus_client PROPERTIES
    POSITION_INDEPENDENT_CODE ON
)

target_link_libraries(yubikey_dbus_client
    Qt6::Core
    Qt6::DBus
    KF6::I18n
)

target_include_directories(yubikey_dbus_client PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
)

# Make types available to other modules
target_include_directories(yubikey_dbus_client INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}
)
