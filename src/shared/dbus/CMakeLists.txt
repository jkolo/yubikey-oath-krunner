# YubiKey D-Bus Client Library
# Shared between KRunner plugin and Config module

# Find qdbusxml2cpp tool
find_package(Qt6 REQUIRED COMPONENTS DBusTools)
get_target_property(QT_DBUSXML2CPP Qt6::qdbusxml2cpp IMPORTED_LOCATION)

# Generate D-Bus interface from daemon's XML
# Use custom command to add -i flag for including yubikey_dbus_types.h
set(DBUS_XML ${CMAKE_CURRENT_SOURCE_DIR}/../../daemon/org.kde.plasma.krunner.yubikey.xml)
set(DBUS_INTERFACE_H ${CMAKE_CURRENT_BINARY_DIR}/yubikey_dbus_interface.h)
set(DBUS_INTERFACE_CPP ${CMAKE_CURRENT_BINARY_DIR}/yubikey_dbus_interface.cpp)

add_custom_command(
    OUTPUT ${DBUS_INTERFACE_H} ${DBUS_INTERFACE_CPP}
    COMMAND ${QT_DBUSXML2CPP}
            -p yubikey_dbus_interface
            -i yubikey_dbus_types.h
            ${DBUS_XML}
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
    DEPENDS ${DBUS_XML}
    COMMENT "Generating D-Bus interface from XML"
)

set(DBUS_INTERFACE_SRCS ${DBUS_INTERFACE_H} ${DBUS_INTERFACE_CPP})

# Static library for D-Bus client (shared by krunner and config)
add_library(yubikey_dbus_client STATIC
    yubikey_dbus_client.cpp
    yubikey_dbus_types.cpp
    ${DBUS_INTERFACE_SRCS}
)

# Enable PIC for static library to allow linking with shared libraries
set_target_properties(yubikey_dbus_client PROPERTIES
    POSITION_INDEPENDENT_CODE ON
)

target_link_libraries(yubikey_dbus_client
    Qt6::Core
    Qt6::DBus
)

target_include_directories(yubikey_dbus_client PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
)

# Make types available to other modules
target_include_directories(yubikey_dbus_client INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}
)
