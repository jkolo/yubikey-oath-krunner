# YubiKey D-Bus Client Library
# Shared between KRunner plugin and Config module
# Uses hierarchical proxy architecture (Manager → Device → Credential)

# Static library for D-Bus client (shared by krunner and config)
add_library(yubikey_dbus_client STATIC
    oath_credential_proxy.cpp
    oath_device_proxy.cpp
    oath_device_session_proxy.cpp
    oath_manager_proxy.cpp
    ../types/oath_credential.cpp
    ../types/yubikey_value_types.cpp
    ../types/yubikey_model.cpp
    ../types/device_model.cpp
    ../types/device_brand.cpp
    ../types/device_capabilities.cpp
    ../types/device_state.cpp
    ../utils/version.cpp
    ../formatting/credential_formatter.cpp
)

# Enable PIC for static library to allow linking with shared libraries
# Enable AUTOMOC for Qt signal/slot meta-object generation
set_target_properties(yubikey_dbus_client PROPERTIES
    POSITION_INDEPENDENT_CODE ON
    AUTOMOC ON
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
