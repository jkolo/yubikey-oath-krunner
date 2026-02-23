# Unit Tests for KRunner YubiKey OATH Plugin
cmake_minimum_required(VERSION 3.16)

# Find Qt Test module
find_package(Qt6 REQUIRED COMPONENTS Test Core)

# Disable clang-tidy for all test targets
# Tests should not have the same strict requirements as production code
set(CMAKE_CXX_CLANG_TIDY "")

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

# Test: AsyncResult<T> template (async operation wrapper)
add_yubikey_test(test_async_result
    SOURCES test_async_result.cpp
    LIBRARIES Qt6::Concurrent
)

# Test: PcscWorkerPool (thread pool with rate limiting)
add_yubikey_test(test_pcsc_worker_pool
    SOURCES test_pcsc_worker_pool.cpp
            ../src/daemon/infrastructure/pcsc_worker_pool.cpp
            ../src/daemon/logging_categories.cpp
    LIBRARIES Qt6::Concurrent KF6::I18n
)

# Test: CredentialFinder (utility function)
add_yubikey_test(test_credential_finder
    SOURCES test_credential_finder.cpp
            ../src/shared/utils/credential_finder.cpp
            ../src/shared/types/oath_credential.cpp
            ../src/shared/formatting/credential_formatter.cpp
            ../src/shared/types/yubikey_value_types.cpp
            ../src/shared/utils/version.cpp
    LIBRARIES Qt6::DBus KF6::I18n
)

# Test: YubiKeyIconResolver (icon path resolution with fallback)
add_yubikey_test(test_yubikey_icon_resolver
    SOURCES test_yubikey_icon_resolver.cpp
            ../src/shared/utils/yubikey_icon_resolver.cpp
            ../src/shared/types/yubikey_model.cpp
            ../src/shared/utils/version.cpp
    LIBRARIES Qt6::DBus
)

# Test: SecureMemory (secure memory wiping for passwords)
add_yubikey_test(test_secure_memory
    SOURCES test_secure_memory.cpp
            ../src/daemon/utils/secure_memory.cpp
)

# Test: CredentialIdEncoder (D-Bus object path encoding)
add_yubikey_test(test_credential_id_encoder
    SOURCES test_credential_id_encoder.cpp
            ../src/daemon/utils/credential_id_encoder.cpp
)

# Test: PasswordDerivation (PBKDF2-HMAC-SHA1 key derivation)
add_yubikey_test(test_password_derivation
    SOURCES test_password_derivation.cpp
)

# Test: SecureLogging (sensitive data masking in logs)
add_yubikey_test(test_secure_logging
    SOURCES test_secure_logging.cpp
)

# Test: DeviceReconnectCoordinator (reconnection lifecycle with signals)
add_yubikey_test(test_device_reconnect_coordinator
    SOURCES test_device_reconnect_coordinator.cpp
            ../src/daemon/infrastructure/device_reconnect_coordinator.cpp
            ../src/daemon/logging_categories.cpp
    LIBRARIES KF6::I18n
)

# Test: NotificationUtils + NotificationHelper (notification hints and timer progress)
add_yubikey_test(test_notification_utils
    SOURCES test_notification_utils.cpp
            ../src/daemon/workflows/notification_utils.cpp
            ../src/daemon/workflows/notification_helper.cpp
            ../src/daemon/formatting/code_validator.cpp
            ../src/daemon/logging_categories.cpp
)

# Test: ManagementProtocol (YubiKey Management interface)
add_yubikey_test(test_management_protocol
    SOURCES test_management_protocol.cpp
            ../src/daemon/oath/management_protocol.cpp
            ../src/daemon/logging_categories.cpp
            ../src/shared/types/yubikey_model.cpp
            ../src/shared/utils/version.cpp
    LIBRARIES Qt6::DBus KF6::I18n
)

# Test: OathProtocol
add_yubikey_test(test_oath_protocol
    SOURCES test_oath_protocol.cpp
            ../src/daemon/oath/oath_protocol.cpp
            ../src/daemon/oath/yk_oath_protocol.cpp
            ../src/daemon/oath/nitrokey_secrets_oath_protocol.cpp
            ../src/daemon/logging_categories.cpp
            ../src/shared/utils/version.cpp
    LIBRARIES KF6::I18n Qt6::DBus
)

# Test: DeviceBrand (brand detection and utility functions)
add_yubikey_test(test_brand_detection
    SOURCES test_brand_detection.cpp
            ../src/shared/types/device_brand.cpp
            ../src/shared/utils/version.cpp
    LIBRARIES KF6::I18n Qt6::DBus
)

# Test: NitrokeyModelDetector (Nitrokey model detection and firmware heuristics)
add_yubikey_test(test_nitrokey_model_detector
    SOURCES test_nitrokey_model_detector.cpp
            ../src/daemon/oath/nitrokey_model_detector.cpp
            ../src/shared/types/device_brand.cpp
            ../src/shared/utils/version.cpp
            ../src/daemon/logging_categories.cpp
    LIBRARIES KF6::I18n Qt6::DBus
)

# Test: DeviceCapabilities (brand-specific protocol capabilities)
add_yubikey_test(test_device_capabilities
    SOURCES test_device_capabilities.cpp
            ../src/shared/types/device_capabilities.cpp
            ../src/shared/types/device_brand.cpp
            ../src/shared/utils/version.cpp
    LIBRARIES KF6::I18n Qt6::DBus
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
            ../src/daemon/formatting/code_validator.cpp
)

# Test: CredentialFormatter formatWithCode and detailed formatting (requires I18n and DBus)
find_package(KF6 COMPONENTS I18n QUIET)
if(KF6_I18n_FOUND)
    add_yubikey_test(test_flexible_display_strategy
        SOURCES test_flexible_display_strategy.cpp
                ../src/shared/formatting/credential_formatter.cpp
                ../src/shared/types/oath_credential.cpp
                ../src/shared/types/yubikey_value_types.cpp
                ../src/shared/utils/version.cpp
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
                ../src/shared/formatting/credential_formatter.cpp
                ../src/shared/types/oath_credential.cpp
                ../src/shared/types/yubikey_value_types.cpp
                ../src/shared/utils/version.cpp
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
                ../src/krunner/matching/match_builder.cpp
                ../src/krunner/logging_categories.cpp
                ../src/shared/utils/version.cpp
                ../src/shared/utils/yubikey_icon_resolver.cpp
                ../src/shared/types/yubikey_model.cpp
        LIBRARIES KF6::Runner KF6::I18n Qt6::DBus yubikey_dbus_client
    )
else()
    message(STATUS "KRunner or I18n not found - skipping test_match_builder")
endif()

# Test: ModifierKeyChecker (requires Gui, I18n, and X11)
# Note: Qt6 Gui is already found at the top of this file via Qt6::Test dependency
find_package(KF6 COMPONENTS I18n QUIET)
find_package(X11 QUIET)
if(KF6_I18n_FOUND AND X11_FOUND)
    add_yubikey_test(test_modifier_key_checker
        SOURCES test_modifier_key_checker.cpp
                ../src/daemon/input/modifier_key_checker.cpp
                ../src/daemon/logging_categories.cpp
        LIBRARIES Qt6::Gui Qt6::Widgets KF6::I18n ${X11_LIBRARIES}
    )
else()
    message(STATUS "I18n or X11 not found - skipping test_modifier_key_checker")
endif()

# Test: YubiKey Proxy Architecture (moved to E2E tests section)
# See E2E tests section below (after add_e2e_test function definition)

# Test: YubiKey Proxy Unit Tests (unit test with mock D-Bus service)
# This test uses a mock D-Bus service to test proxy classes in isolation
add_yubikey_test(test_proxy_unit
    SOURCES test_proxy_unit.cpp
            ../src/shared/types/yubikey_value_types.cpp
            ../src/shared/utils/version.cpp
    LIBRARIES Qt6::DBus yubikey_dbus_client
)

# Test: TouchHandler (workflow component)
add_yubikey_test(test_touch_handler
    SOURCES test_touch_handler.cpp
            ../src/daemon/workflows/touch_handler.cpp
            ../src/daemon/logging_categories.cpp
)

# Test: ActionExecutor (action execution with fallback)
find_package(KF6 COMPONENTS GuiAddons QUIET)
find_package(PkgConfig QUIET)
if(PKG_CONFIG_FOUND)
    pkg_check_modules(X11 IMPORTED_TARGET x11)
endif()

if(KF6_GuiAddons_FOUND AND X11_FOUND)
    add_yubikey_test(test_action_executor
        SOURCES test_action_executor.cpp
                mocks/mock_configuration_provider.cpp
                mocks/mock_clipboard_manager.cpp
                mocks/mock_text_input_provider.cpp
                mocks/mock_dbus_notification_manager.cpp
                mocks/mock_notification_orchestrator.cpp
                ../src/daemon/actions/action_executor.cpp
                ../src/daemon/input/modifier_key_checker.cpp
                ../src/daemon/clipboard/clipboard_manager.cpp
                ../src/daemon/workflows/notification_orchestrator.cpp
                ../src/daemon/workflows/notification_helper.cpp
                ../src/daemon/workflows/notification_utils.cpp
                ../src/daemon/notification/dbus_notification_manager.cpp
                ../src/daemon/formatting/code_validator.cpp
                ../src/daemon/logging_categories.cpp
                ../src/shared/utils/yubikey_icon_resolver.cpp
                ../src/shared/types/yubikey_model.cpp
                ../src/shared/types/device_brand.cpp
                ../src/shared/utils/version.cpp
        LIBRARIES Qt6::Gui Qt6::Widgets Qt6::DBus KF6::I18n KF6::Notifications KF6::GuiAddons PkgConfig::X11
    )
else()
    message(STATUS "GuiAddons or X11 not found - skipping test_action_executor")
endif()

# Test: TouchWorkflowCoordinator (workflow component integration tests)
add_yubikey_test(test_touch_workflow_coordinator
    SOURCES test_touch_workflow_coordinator.cpp
            mocks/mock_touch_handler.cpp
            mocks/mock_oath_action_coordinator.cpp
            mocks/mock_configuration_provider.cpp
            mocks/mock_dbus_notification_manager.cpp
            mocks/mock_notification_orchestrator.cpp
            ../src/daemon/workflows/touch_handler.cpp
            ../src/daemon/workflows/notification_orchestrator.cpp
            ../src/daemon/workflows/notification_helper.cpp
            ../src/daemon/workflows/notification_utils.cpp
            ../src/daemon/notification/dbus_notification_manager.cpp
            ../src/daemon/formatting/code_validator.cpp
            ../src/daemon/logging_categories.cpp
            ../src/shared/utils/yubikey_icon_resolver.cpp
            ../src/shared/types/yubikey_model.cpp
            ../src/shared/types/device_brand.cpp
            ../src/shared/utils/version.cpp
    LIBRARIES Qt6::Core Qt6::DBus KF6::I18n KF6::Notifications
)

# Test: ClipboardManager (clipboard operations with auto-clear)
find_package(KF6 COMPONENTS GuiAddons QUIET)
if(KF6_GuiAddons_FOUND)
    add_yubikey_test(test_clipboard_manager
        SOURCES test_clipboard_manager.cpp
                ../src/daemon/clipboard/clipboard_manager.cpp
                ../src/daemon/logging_categories.cpp
        LIBRARIES Qt6::Core Qt6::Gui KF6::GuiAddons
    )
else()
    message(STATUS "GuiAddons not found - skipping test_clipboard_manager")
endif()

# Test: NotificationOrchestrator (notification management with countdown timers)
add_yubikey_test(test_notification_orchestrator
    SOURCES test_notification_orchestrator.cpp
            mocks/mock_dbus_notification_manager.cpp
            mocks/mock_configuration_provider.cpp
            ../src/daemon/workflows/notification_orchestrator.cpp
            ../src/daemon/workflows/notification_helper.cpp
            ../src/daemon/workflows/notification_utils.cpp
            ../src/daemon/notification/dbus_notification_manager.cpp
            ../src/daemon/formatting/code_validator.cpp
            ../src/daemon/logging_categories.cpp
            ../src/shared/utils/yubikey_icon_resolver.cpp
            ../src/shared/types/yubikey_model.cpp
            ../src/shared/types/device_brand.cpp
            ../src/shared/utils/version.cpp
    LIBRARIES Qt6::Core Qt6::DBus KF6::I18n KF6::Notifications
)

# Test: RelativeTimeFormatter (Config UI utility)
add_yubikey_test(test_relative_time_formatter
    SOURCES test_relative_time_formatter.cpp
            ../src/config/relative_time_formatter.cpp
    LIBRARIES KF6::I18n
)

# Test: IDeviceIconResolver (Interface compliance and ISP verification)
add_yubikey_test(test_device_icon_resolver
    SOURCES test_device_icon_resolver.cpp
)

# Test: DeviceCardLayout (Config UI layout calculator)
add_yubikey_test(test_device_card_layout
    SOURCES test_device_card_layout.cpp
            ../src/config/device_card_layout.cpp
    LIBRARIES Qt6::Widgets
)

# Test: DeviceDelegate - SKIPPED (too complex for unit testing without refactoring)
# DeviceDelegate requires YubiKeyConfig* which pulls in entire KCModule UI dependencies
# (including generated ui_oath_config.h from .ui files).
# Will be testable after Phase 5 refactoring splits DeviceDelegate into smaller components.
# TODO: Add tests after refactoring DeviceDelegate → DeviceCardRenderer, DeviceActionHandler, DeviceNameEditor

# ============================================================================
# Service Layer Tests (Phase 1 - TEST_IMPLEMENTATION.md)
# ============================================================================

# Test: PasswordService (password validation and KWallet integration)
# Uses: MockYubiKeyDeviceManager, MockYubiKeyOathDevice, MockSecretStorage, TestDeviceFixture
# Target coverage: 100% (security-critical component)
find_package(KF6 COMPONENTS Wallet QUIET)
find_package(PkgConfig QUIET)
if(KF6_Wallet_FOUND AND PKG_CONFIG_FOUND)
    pkg_check_modules(PCSCLITE libpcsclite)
    if(PCSCLITE_FOUND)
        add_yubikey_test(test_password_service
            SOURCES test_password_service.cpp
                    mocks/mock_oath_database.cpp
                    mocks/mock_oath_device_manager.cpp
                    mocks/mock_oath_device.cpp
                    mocks/mock_secret_storage.cpp
                    ../src/daemon/services/password_service.cpp
                    ../src/daemon/storage/oath_database.cpp
                    ../src/daemon/storage/secret_storage.cpp
                    ../src/daemon/storage/transaction_guard.cpp
                    ../src/daemon/oath/oath_device.cpp
                    ../src/daemon/oath/oath_device_manager.cpp
                    ../src/daemon/oath/yk_oath_session.cpp
                    ../src/daemon/oath/extended_device_info_fetcher.cpp
                    ../src/daemon/pcsc/card_transaction.cpp
                    ../src/daemon/oath/oath_protocol.cpp
                    ../src/daemon/oath/yk_oath_protocol.cpp
                    ../src/daemon/oath/nitrokey_secrets_oath_protocol.cpp
                    ../src/daemon/oath/yubikey_oath_device.cpp
                    ../src/daemon/oath/nitrokey_oath_device.cpp
                    ../src/daemon/oath/nitrokey_oath_session.cpp
                    ../src/daemon/oath/nitrokey_model_detector.cpp
                    ../src/daemon/oath/management_protocol.cpp
                    ../src/daemon/pcsc/card_reader_monitor.cpp
                    ../src/daemon/infrastructure/pcsc_worker_pool.cpp
                    ../src/daemon/infrastructure/device_reconnect_coordinator.cpp
                    ../src/daemon/utils/secure_memory.cpp
                    ../src/daemon/logging_categories.cpp
                    ../src/shared/types/device_state.cpp
                    ../src/shared/types/device_brand.cpp
                    ../src/shared/types/yubikey_model.cpp
                    ../src/shared/types/device_model.cpp
                    ../src/shared/types/device_capabilities.cpp
                    ../src/shared/types/oath_credential.cpp
                    ../src/shared/utils/version.cpp
            LIBRARIES Qt6::DBus Qt6::Sql Qt6::Concurrent KF6::I18n KF6::Wallet ${PCSCLITE_LIBRARIES}
        )
        target_include_directories(test_password_service PRIVATE ${PCSCLITE_INCLUDE_DIRS})

        # Phase 1.4: DeviceLifecycleService tests
        add_yubikey_test(test_device_lifecycle_service
            SOURCES test_device_lifecycle_service.cpp
                    mocks/mock_oath_database.cpp
                    mocks/mock_oath_device_manager.cpp
                    mocks/mock_oath_device.cpp
                    mocks/mock_secret_storage.cpp
                    ../src/daemon/services/device_lifecycle_service.cpp
                    ../src/daemon/storage/oath_database.cpp
                    ../src/daemon/storage/secret_storage.cpp
                    ../src/daemon/storage/transaction_guard.cpp
                    ../src/daemon/oath/oath_device.cpp
                    ../src/daemon/oath/oath_device_manager.cpp
                    ../src/daemon/oath/yk_oath_session.cpp
                    ../src/daemon/oath/extended_device_info_fetcher.cpp
                    ../src/daemon/pcsc/card_transaction.cpp
                    ../src/daemon/oath/oath_protocol.cpp
                    ../src/daemon/oath/yk_oath_protocol.cpp
                    ../src/daemon/oath/nitrokey_secrets_oath_protocol.cpp
                    ../src/daemon/oath/yubikey_oath_device.cpp
                    ../src/daemon/oath/nitrokey_oath_device.cpp
                    ../src/daemon/oath/nitrokey_oath_session.cpp
                    ../src/daemon/oath/nitrokey_model_detector.cpp
                    ../src/daemon/oath/management_protocol.cpp
                    ../src/daemon/pcsc/card_reader_monitor.cpp
                    ../src/daemon/infrastructure/pcsc_worker_pool.cpp
                    ../src/daemon/infrastructure/device_reconnect_coordinator.cpp
                    ../src/daemon/utils/secure_memory.cpp
                    ../src/daemon/logging_categories.cpp
                    ../src/shared/types/device_state.cpp
                    ../src/shared/types/device_brand.cpp
                    ../src/shared/types/yubikey_model.cpp
                    ../src/shared/types/device_model.cpp
                    ../src/shared/types/device_capabilities.cpp
                    ../src/shared/types/oath_credential.cpp
                    ../src/shared/utils/version.cpp
                    ../src/shared/utils/device_name_formatter.cpp
            LIBRARIES Qt6::DBus Qt6::Sql Qt6::Concurrent KF6::I18n KF6::Wallet ${PCSCLITE_LIBRARIES}
        )
        target_include_directories(test_device_lifecycle_service PRIVATE ${PCSCLITE_INCLUDE_DIRS})

        # Phase 1.5: CredentialService tests
        add_yubikey_test(test_credential_service
            SOURCES test_credential_service.cpp
                    mocks/mock_oath_database.cpp
                    mocks/mock_oath_device_manager.cpp
                    mocks/mock_oath_device.cpp
                    mocks/mock_daemon_configuration.cpp
                    ../src/daemon/services/credential_service.cpp
                    ../src/daemon/storage/oath_database.cpp
                    ../src/daemon/storage/transaction_guard.cpp
                    ../src/daemon/oath/oath_device.cpp
                    ../src/daemon/oath/oath_device_manager.cpp
                    ../src/daemon/oath/yk_oath_session.cpp
                    ../src/daemon/oath/extended_device_info_fetcher.cpp
                    ../src/daemon/pcsc/card_transaction.cpp
                    ../src/daemon/oath/oath_protocol.cpp
                    ../src/daemon/oath/yk_oath_protocol.cpp
                    ../src/daemon/oath/nitrokey_secrets_oath_protocol.cpp
                    ../src/daemon/oath/yubikey_oath_device.cpp
                    ../src/daemon/oath/nitrokey_oath_device.cpp
                    ../src/daemon/oath/nitrokey_oath_session.cpp
                    ../src/daemon/oath/nitrokey_model_detector.cpp
                    ../src/daemon/oath/management_protocol.cpp
                    ../src/daemon/pcsc/card_reader_monitor.cpp
                    ../src/daemon/infrastructure/pcsc_worker_pool.cpp
                    ../src/daemon/infrastructure/device_reconnect_coordinator.cpp
                    ../src/daemon/utils/secure_memory.cpp
                    ../src/daemon/logging_categories.cpp
                    ../src/daemon/ui/add_credential_dialog.cpp
                    ../src/daemon/ui/processing_overlay.cpp
                    ../src/daemon/notification/dbus_notification_manager.cpp
                    ../src/daemon/utils/screenshot_capturer.cpp
                    ../src/daemon/utils/qr_code_parser.cpp
                    ../src/daemon/utils/otpauth_uri_parser.cpp
                    ../src/shared/utils/device_name_formatter.cpp
                    ../src/shared/types/device_state.cpp
                    ../src/shared/types/device_brand.cpp
                    ../src/shared/types/yubikey_model.cpp
                    ../src/shared/types/device_model.cpp
                    ../src/shared/types/device_capabilities.cpp
                    ../src/shared/types/oath_credential.cpp
                    ../src/shared/types/yubikey_value_types.cpp
                    ../src/shared/utils/version.cpp
            LIBRARIES Qt6::DBus Qt6::Sql Qt6::Concurrent Qt6::Widgets KF6::I18n KF6::Notifications KF6::WidgetsAddons ZXing::ZXing ${PCSCLITE_LIBRARIES}
        )
        target_include_directories(test_credential_service PRIVATE ${PCSCLITE_INCLUDE_DIRS})
    else()
        message(STATUS "PCSCLite not found - skipping test_password_service, test_device_lifecycle_service, and test_credential_service")
    endif()
else()
    message(STATUS "KWallet or PkgConfig not found - skipping test_password_service and test_device_lifecycle_service")
endif()

# ============================================================================
# Storage Layer Tests (Phase 2)
# ============================================================================

# test_oath_database - YubiKeyDatabase SQLite operations test
add_executable(test_oath_database
    test_oath_database.cpp
    ../src/daemon/storage/oath_database.cpp
    ../src/daemon/storage/transaction_guard.cpp
    ../src/daemon/logging_categories.cpp
    ../src/shared/types/oath_credential.cpp
    ../src/shared/types/yubikey_model.cpp
    ../src/shared/utils/version.cpp
)

target_link_libraries(test_oath_database
    Qt6::Test
    Qt6::Core
    Qt6::Sql
    Qt6::DBus
    KF6::I18n
)

target_include_directories(test_oath_database PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}
)

# Disable strict ASCII casting for tests
target_compile_options(test_oath_database PRIVATE
    -UQT_NO_CAST_FROM_ASCII
)

add_test(NAME test_oath_database COMMAND test_oath_database)

# test_secret_storage - SecretStorage API test (using mock)
add_executable(test_secret_storage
    test_secret_storage.cpp
    mocks/mock_secret_storage.cpp
    ../src/daemon/storage/secret_storage.cpp
    ../src/daemon/logging_categories.cpp
    ../src/daemon/utils/secure_memory.cpp
)

target_link_libraries(test_secret_storage
    Qt6::Test
    Qt6::Core
    KF6::I18n
    KF6::Wallet
)

target_include_directories(test_secret_storage PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_compile_options(test_secret_storage PRIVATE
    -UQT_NO_CAST_FROM_ASCII
)

add_test(NAME test_secret_storage COMMAND test_secret_storage)

# ============================================================================
# D-Bus Object Layer Tests
# ============================================================================

# Test 7: OathManagerObject - ObjectManager D-Bus interface
# SKIPPED: Requires interface refactoring (see TEST_IMPLEMENTATION.md Phase 3)
# Coverage provided via test_e2e_device_lifecycle (~90% D-Bus coverage)
#[[
add_executable(test_oath_manager_object
    test_oath_manager_object.cpp
    mocks/mock_oath_service.cpp
    ../src/daemon/dbus/oath_manager_object.cpp
    ../src/daemon/dbus/oath_device_object.cpp
    ../src/daemon/dbus/oath_credential_object.cpp
    ../src/daemon/services/oath_service.cpp
    ../src/daemon/services/password_service.cpp
    ../src/daemon/services/device_lifecycle_service.cpp
    ../src/daemon/services/credential_service.cpp
    ../src/daemon/workflows/notification_orchestrator.cpp
    ../src/daemon/workflows/touch_workflow_coordinator.cpp
    ../src/daemon/workflows/touch_handler.cpp
    ../src/daemon/actions/action_executor.cpp
    ../src/daemon/actions/oath_action_coordinator.cpp
    ../src/daemon/clipboard/clipboard_manager.cpp
    ../src/daemon/oath/oath_device_manager.cpp
    ../src/daemon/oath/yubikey_oath_device.cpp
    ../src/daemon/oath/nitrokey_oath_device.cpp
    ../src/daemon/oath/oath_device.cpp
    ../src/daemon/oath/nitrokey_oath_session.cpp
    ../src/daemon/oath/yk_oath_session.cpp
    ../src/daemon/oath/extended_device_info_fetcher.cpp
    ../src/daemon/oath/oath_protocol.cpp
    ../src/daemon/oath/management_protocol.cpp
    ../src/daemon/oath/nitrokey_model_detector.cpp
    ../src/daemon/storage/oath_database.cpp
    ../src/daemon/storage/secret_storage.cpp
    ../src/daemon/utils/secure_memory.cpp
    ../src/daemon/logging_categories.cpp
    ../src/daemon/config/daemon_configuration.cpp
    ../src/shared/types/yubikey_value_types.cpp
    ../src/shared/types/oath_credential.cpp
    ../src/shared/types/device_brand.cpp
    ../src/daemon/types/device_model.cpp
    ../src/shared/types/device_capabilities.cpp
    ../src/shared/types/device_state.cpp
    ../src/shared/types/yubikey_model.cpp
    ../src/shared/formatting/credential_formatter.cpp
    ../src/shared/utils/credential_finder.cpp
    ../src/shared/utils/version.cpp
    ../src/daemon/storage/transaction_guard.cpp
    ../src/daemon/pcsc/card_reader_monitor.cpp
    ../src/daemon/infrastructure/pcsc_worker_pool.cpp
    ../src/daemon/infrastructure/device_reconnect_coordinator.cpp
    ../src/daemon/oath/yk_oath_protocol.cpp
    ../src/daemon/oath/nitrokey_secrets_oath_protocol.cpp
    ../src/daemon/workflows/notification_utils.cpp
)

target_link_libraries(test_oath_manager_object
    Qt6::Test
    Qt6::Core
    Qt6::DBus
    Qt6::Sql
    Qt6::Concurrent
    KF6::I18n
    KF6::Wallet
    KF6::Notifications
    KF6::ConfigCore
    ${PCSCLITE_LIBRARIES}
    ZXing::ZXing
)

target_include_directories(test_oath_manager_object PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_BINARY_DIR}/src/daemon  # For generated adaptors
    ${PCSCLITE_INCLUDE_DIRS}
)

# Disable strict ASCII casting for tests
target_compile_options(test_oath_manager_object PRIVATE
    -UQT_NO_CAST_FROM_ASCII
)

# Enable coverage if requested
if(ENABLE_COVERAGE)
    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(test_oath_manager_object PRIVATE --coverage)
        target_link_options(test_oath_manager_object PRIVATE --coverage)
    endif()
endif()

add_test(NAME test_oath_manager_object COMMAND test_oath_manager_object)
]]

# ============================================================================
# E2E Test Infrastructure
# ============================================================================

# Virtual device emulators library (shared by E2E tests)
add_library(virtual_oath_devices STATIC
    mocks/virtual_oath_device.cpp
    mocks/virtual_yubikey.cpp
    mocks/virtual_nitrokey.cpp
    ../src/daemon/oath/oath_protocol.cpp  # For OathProtocol::findTlvTag
    ../src/daemon/logging_categories.cpp  # For YubiKeyOathDeviceLog()
)

target_link_libraries(virtual_oath_devices
    Qt6::Core
    Qt6::DBus
    KF6::I18n  # Required for OathCredential includes
)

target_include_directories(virtual_oath_devices PUBLIC
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}
)

# Disable strict ASCII casting for test infrastructure
target_compile_options(virtual_oath_devices PRIVATE
    -UQT_NO_CAST_FROM_ASCII
)

# TestDbusSession helper library
add_library(test_dbus_session STATIC
    helpers/test_dbus_session.cpp
)

target_link_libraries(test_dbus_session
    Qt6::Core
    Qt6::DBus
    Qt6::Test
)

target_include_directories(test_dbus_session PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
)

# Helper function for E2E tests (runs with dbus-run-session for isolation)
function(add_e2e_test TEST_NAME)
    cmake_parse_arguments(TEST "" "" "SOURCES;LIBRARIES" ${ARGN})

    # Create test executable
    add_executable(${TEST_NAME} ${TEST_SOURCES})

    # Link required libraries
    target_link_libraries(${TEST_NAME}
        Qt6::Test
        Qt6::Core
        Qt6::DBus
        virtual_oath_devices
        test_dbus_session
        yubikey_dbus_client  # For OathManagerProxy, OathDeviceProxy
        ${TEST_LIBRARIES}
    )

    # Add include directories
    target_include_directories(${TEST_NAME} PRIVATE
        ${CMAKE_SOURCE_DIR}/src
        ${CMAKE_CURRENT_SOURCE_DIR}
    )

    # Disable strict ASCII casting for tests
    target_compile_options(${TEST_NAME} PRIVATE
        -UQT_NO_CAST_FROM_ASCII
    )

    # Add test (test manages its own D-Bus session via TestDbusSession)
    # TestDbusSession provides proper cleanup order: daemon → D-Bus session
    add_test(
        NAME ${TEST_NAME}
        COMMAND $<TARGET_FILE:${TEST_NAME}>
    )

    # Resource lock to prevent parallel PC/SC access conflicts
    set_tests_properties(${TEST_NAME} PROPERTIES
        RESOURCE_LOCK pcsc_daemon
        TIMEOUT 120
    )

    # Enable coverage if requested
    if(ENABLE_COVERAGE)
        if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
            target_compile_options(${TEST_NAME} PRIVATE --coverage)
            target_link_options(${TEST_NAME} PRIVATE --coverage)
        endif()
    endif()
endfunction()

# E2E Test: Device Lifecycle (detection → auth → fetch → disconnect)
add_e2e_test(test_e2e_device_lifecycle
    SOURCES test_e2e_device_lifecycle.cpp
    LIBRARIES KF6::I18n
)

# E2E Test: YubiKey Proxy Architecture (D-Bus proxy layer with isolated daemon)
# Tests OathManagerProxy, OathDeviceProxy, OathCredentialProxy against real daemon
# NOTE: Tests that require physical devices will QSKIP (PC/SC virtual device injection not yet implemented)
add_e2e_test(test_yubikey_proxy
    SOURCES test_yubikey_proxy.cpp
            ../src/shared/utils/version.cpp
    LIBRARIES KF6::I18n
)

# Summary
message(STATUS "Unit tests configured:")
message(STATUS "  - test_result (Result<T> template)")
message(STATUS "  - test_async_result (AsyncResult<T> async operation wrapper)")
message(STATUS "  - test_pcsc_worker_pool (PcscWorkerPool thread pool with rate limiting)")
message(STATUS "  - test_credential_finder (CredentialFinder utility)")
message(STATUS "  - test_yubikey_icon_resolver (YubiKeyIconResolver utility)")
message(STATUS "  - test_management_protocol (ManagementProtocol - YubiKey Management interface)")
message(STATUS "  - test_code_validator (CodeValidator)")
message(STATUS "  - test_credential_formatter (CredentialFormatter)")
message(STATUS "  - test_credential_id_encoder (CredentialIdEncoder - D-Bus path encoding)")
message(STATUS "  - test_password_derivation (PasswordDerivation - PBKDF2-HMAC-SHA1)")
message(STATUS "  - test_secure_logging (SecureLogging - sensitive data masking)")
message(STATUS "  - test_yubikey_proxy (Proxy architecture E2E - isolated D-Bus, skips tests requiring physical devices)")
message(STATUS "  - test_proxy_unit (Proxy architecture unit tests - mock D-Bus service)")
message(STATUS "  - test_touch_handler (TouchHandler workflow)")
message(STATUS "  - test_action_executor (ActionExecutor with fallback)")
message(STATUS "  - test_touch_workflow_coordinator (Touch workflow integration)")
message(STATUS "  - test_clipboard_manager (ClipboardManager with auto-clear)")
message(STATUS "  - test_notification_orchestrator (NotificationOrchestrator with timers)")
message(STATUS "  - test_relative_time_formatter (RelativeTimeFormatter utility)")
message(STATUS "  - test_device_icon_resolver (IDeviceIconResolver interface compliance)")
message(STATUS "  - test_device_card_layout (DeviceCardLayout - UI layout calculator)")
# message(STATUS "  - test_device_delegate (SKIPPED - requires refactoring)")
if(KF6_Runner_FOUND)
    message(STATUS "  - test_match_builder (MatchBuilder)")
    message(STATUS "  - test_modifier_key_checker (ModifierKeyChecker)")
endif()
