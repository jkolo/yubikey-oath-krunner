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

    # Daemon utilities
    logging_categories.cpp
)

# Include directories
target_include_directories(yubikey-oath-daemon PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/..
    ${PCSCLITE_INCLUDE_DIRS}
)

# Link libraries
target_link_libraries(yubikey-oath-daemon
    Qt6::Core
    Qt6::DBus
    Qt6::Sql
    Qt6::Concurrent
    KF6::I18n
    KF6::Wallet
    ${PCSCLITE_LIBRARIES}
)

# Compiler flags for PC/SC
target_compile_definitions(yubikey-oath-daemon PRIVATE
    ${PCSCLITE_CFLAGS_OTHER}
)

# Installation
install(TARGETS yubikey-oath-daemon
        DESTINATION ${KDE_INSTALL_BINDIR})

# D-Bus service activation file
install(FILES org.kde.plasma.krunner.yubikey.service
        DESTINATION ${KDE_INSTALL_DBUSSERVICEDIR})
