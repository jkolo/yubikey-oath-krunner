cmake_minimum_required(VERSION 3.16)
project(krunner-yubikey VERSION 2.0.0)

set(CMAKE_CXX_STANDARD 26)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Export compile commands for clang-tidy and other tools
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Compiler warnings
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    add_compile_options(
        -Wall
        -Wextra
        -Wpedantic
        -Wunused
        -Wunused-function
        -Wunused-parameter
        -Wunused-variable
        -Wshadow
        -Wcast-align
        -Woverloaded-virtual
    )
    message(STATUS "Compiler warnings enabled")
endif()

# Clang-Tidy integration
# Configuration is read from .clang-tidy file in project root
option(ENABLE_CLANG_TIDY "Enable clang-tidy analysis" ON)
if(ENABLE_CLANG_TIDY)
    find_program(CLANG_TIDY_EXE NAMES "clang-tidy")
    if(CLANG_TIDY_EXE)
        # Use .clang-tidy file for configuration (don't override with CMake settings)
        set(CMAKE_CXX_CLANG_TIDY ${CLANG_TIDY_EXE})
        message(STATUS "clang-tidy enabled: ${CLANG_TIDY_EXE}")
        message(STATUS "  Configuration will be read from .clang-tidy file")
    else()
        message(WARNING "clang-tidy not found")
    endif()
endif()

# Code Coverage Option
option(ENABLE_COVERAGE "Enable code coverage analysis with gcov/lcov" OFF)

if(ENABLE_COVERAGE)
    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        message(STATUS "Code coverage enabled")
        set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} --coverage -fprofile-arcs -ftest-coverage")
        set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} --coverage -fprofile-arcs -ftest-coverage")
        set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} --coverage")
        set(CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS} --coverage")
    else()
        message(WARNING "Code coverage only supported with GCC or Clang compilers")
    endif()
endif()

find_package(ECM 6.0.0 REQUIRED NO_MODULE)
set(CMAKE_MODULE_PATH ${ECM_MODULE_PATH})

include(KDEInstallDirs)
include(KDECMakeSettings)
include(KDECompilerSettings NO_POLICY_SCOPE)
include(FeatureSummary)

# Debug: Check what KDE install directories are available
message(STATUS "KDE_INSTALL_KSERVICESDIR = '${KDE_INSTALL_KSERVICESDIR}'")
message(STATUS "KDE_INSTALL_SERVICESDIR = '${KDE_INSTALL_SERVICESDIR}'")
message(STATUS "KDE_INSTALL_KSERVICES5DIR = '${KDE_INSTALL_KSERVICES5DIR}'")
message(STATUS "KDE_INSTALL_PLUGINDIR = '${KDE_INSTALL_PLUGINDIR}'")
message(STATUS "KDE_INSTALL_DATADIR = '${KDE_INSTALL_DATADIR}'")

find_package(Qt6 REQUIRED COMPONENTS
    Core
    Widgets
    Qml
    Quick
    QuickWidgets
    Gui
    DBus
    Concurrent
    Sql
)

find_package(KF6 REQUIRED COMPONENTS
    Runner
    I18n
    Config
    ConfigWidgets
    Notifications
    CoreAddons
    Wallet
    KCMUtils
    WidgetsAddons
)

find_package(KWayland REQUIRED)

find_package(PkgConfig REQUIRED)
pkg_check_modules(PCSCLITE REQUIRED libpcsclite)
pkg_check_modules(XKBCOMMON REQUIRED xkbcommon)
pkg_check_modules(LIBPORTAL_QT6 REQUIRED IMPORTED_TARGET libportal-qt6)

# Use system ZXing-C++ library for QR code scanning
find_package(ZXing REQUIRED)

# Add shared/ to include paths globally to avoid ../shared/ in includes
include_directories(${CMAKE_SOURCE_DIR}/src/shared)

add_subdirectory(src)

# Enable testing
option(BUILD_TESTING "Build unit tests" ON)
if(BUILD_TESTING)
    enable_testing()
    add_subdirectory(tests)
endif()

feature_summary(WHAT ALL FATAL_ON_MISSING_REQUIRED_PACKAGES)
