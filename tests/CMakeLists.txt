# Unit Tests for KRunner YubiKey OATH Plugin
cmake_minimum_required(VERSION 3.16)

# Find Qt Test module
find_package(Qt6 REQUIRED COMPONENTS Test Core)

# Helper function for creating test executables
function(add_yubikey_test TEST_NAME)
    # Parse arguments: SOURCE_FILES and optional LINK_LIBRARIES
    cmake_parse_arguments(TEST "" "" "SOURCES;LIBRARIES" ${ARGN})

    # Create test executable
    add_executable(${TEST_NAME} ${TEST_SOURCES})

    # Link required libraries
    target_link_libraries(${TEST_NAME}
        Qt6::Test
        Qt6::Core
        ${TEST_LIBRARIES}
    )

    # Add include directories
    target_include_directories(${TEST_NAME} PRIVATE
        ${CMAKE_SOURCE_DIR}/src
        ${CMAKE_CURRENT_SOURCE_DIR}
    )

    # Disable strict ASCII casting for tests (allow string literals)
    # This makes test code more readable
    target_compile_options(${TEST_NAME} PRIVATE
        -UQT_NO_CAST_FROM_ASCII
    )

    # Add test to CTest
    add_test(NAME ${TEST_NAME} COMMAND ${TEST_NAME})

    # Enable coverage if requested
    if(ENABLE_COVERAGE)
        if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
            target_compile_options(${TEST_NAME} PRIVATE --coverage)
            target_link_options(${TEST_NAME} PRIVATE --coverage)
        endif()
    endif()
endfunction()

# Test: Result<T> template
add_yubikey_test(test_result
    SOURCES test_result.cpp
)

# Test: OathProtocol
add_yubikey_test(test_oath_protocol
    SOURCES test_oath_protocol.cpp
            ../src/daemon/oath/oath_protocol.cpp
)

# Test: ActionManager (requires KRunner and I18n)
find_package(KF6 COMPONENTS Runner I18n QUIET)
if(KF6_Runner_FOUND AND KF6_I18n_FOUND)
    add_yubikey_test(test_action_manager
        SOURCES test_action_manager.cpp
                ../src/krunner/actions/action_manager.cpp
                ../src/krunner/logging_categories.cpp
        LIBRARIES KF6::Runner KF6::I18n
    )
else()
    message(STATUS "KRunner or I18n not found - skipping test_action_manager")
endif()

# Test: FlexibleDisplayStrategy (requires I18n) - TODO: Implement test file
# find_package(KF6 COMPONENTS I18n QUIET)
# if(KF6_I18n_FOUND)
#     add_yubikey_test(test_flexible_display_strategy
#         SOURCES test_flexible_display_strategy.cpp
#                 ../src/krunner/formatting/display_strategies/flexible_display_strategy.cpp
#         LIBRARIES KF6::I18n
#     )
# else()
#     message(STATUS "I18n not found - skipping test_flexible_display_strategy")
# endif()

# Test: CodeValidator
add_yubikey_test(test_code_validator
    SOURCES test_code_validator.cpp
            ../src/krunner/formatting/code_validator.cpp
)

# Test: CredentialFormatter formatWithCode and detailed formatting (requires I18n and DBus)
find_package(KF6 COMPONENTS I18n QUIET)
if(KF6_I18n_FOUND)
    add_yubikey_test(test_flexible_display_strategy
        SOURCES test_flexible_display_strategy.cpp
                ../src/krunner/formatting/credential_formatter.cpp
                ../src/shared/dbus/yubikey_dbus_types.cpp
        LIBRARIES KF6::I18n Qt6::DBus
    )
else()
    message(STATUS "I18n not found - skipping test_flexible_display_strategy")
endif()

# Test: CredentialFormatter overloads and type conversion
# Note: Requires DBus for types
find_package(KF6 COMPONENTS I18n QUIET)
if(KF6_I18n_FOUND)
    add_yubikey_test(test_credential_formatter
        SOURCES test_credential_formatter.cpp
                ../src/krunner/formatting/credential_formatter.cpp
                ../src/shared/dbus/yubikey_dbus_types.cpp
        LIBRARIES Qt6::DBus KF6::I18n
    )
else()
    message(STATUS "I18n not found - skipping test_credential_formatter")
endif()

# Test: MatchBuilder
# (requires KRunner and I18n)
find_package(KF6 COMPONENTS Runner I18n QUIET)
if(KF6_Runner_FOUND AND KF6_I18n_FOUND)
    add_yubikey_test(test_match_builder
        SOURCES test_match_builder.cpp
                mocks/mock_configuration_provider.cpp
                ../src/krunner/config/configuration_provider.cpp
                ../src/krunner/matching/match_builder.cpp
                ../src/krunner/formatting/credential_formatter.cpp
                ../src/krunner/logging_categories.cpp
        LIBRARIES KF6::Runner KF6::I18n Qt6::DBus yubikey_dbus_client
    )
else()
    message(STATUS "KRunner or I18n not found - skipping test_match_builder")
endif()

# Summary
message(STATUS "Unit tests configured:")
message(STATUS "  - test_result (Result<T> template)")
message(STATUS "  - test_code_validator (CodeValidator)")
message(STATUS "  - test_credential_formatter (CredentialFormatter)")
if(KF6_Runner_FOUND)
    message(STATUS "  - test_match_builder (MatchBuilder)")
endif()
