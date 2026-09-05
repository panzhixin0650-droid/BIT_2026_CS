cmake_minimum_required(VERSION 3.22)

project(charging_user_client LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC OFF)
set(CMAKE_AUTORCC ON)

option(CHARGING_CLIENT_ENABLE_WEBENGINE
       "Build the embedded Tencent map view with Qt WebEngine"
       OFF)

find_package(Qt6 6.2 REQUIRED COMPONENTS Network Widgets)

include(CTest)

add_subdirectory(
    "${CMAKE_CURRENT_SOURCE_DIR}/../shared/protocol"
    "${CMAKE_CURRENT_BINARY_DIR}/shared-protocol"
)

add_library(charging_client_api STATIC
    src/api/api_result.h
    src/api/i_charging_api.cpp
    src/api/i_charging_api.h
    src/api/mock_charging_api.cpp
    src/api/mock_charging_api.h
    src/api/tcp_charging_api.cpp
    src/api/tcp_charging_api.h
)

target_include_directories(charging_client_api
    PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/src"
)

target_link_libraries(charging_client_api
    PUBLIC
        charging_protocol
        Qt6::Core
        Qt6::Network
)

add_library(charging_client_ui STATIC
    resources/assistant_resources.qrc
    src/assistant/assistant_config.cpp
    src/assistant/assistant_config.h
    src/assistant/assistant_service.cpp
    src/assistant/assistant_service.h
    src/assistant/knowledge_base.cpp
    src/assistant/knowledge_base.h
    src/ui/support_page.cpp
    src/ui/support_page.h
    src/local/avatar_storage.cpp
    src/local/avatar_storage.h
    src/local/i_map_service.cpp
    src/local/i_map_service.h
    src/local/input_method_setup.cpp
    src/local/input_method_setup.h
    src/local/map_types.h
    src/local/mock_map_service.cpp
    src/local/mock_map_service.h
    src/local/tencent_map_service.cpp
    src/local/tencent_map_service.h
    src/ui/api_error_message.h
    src/ui/charging_stop_dialog.cpp
    src/ui/charging_stop_dialog.h
    src/ui/client_theme.cpp
    src/ui/client_theme.h
    src/ui/charging_art.cpp
    src/ui/charging_art.h
    src/ui/login_controller.cpp
    src/ui/login_controller.h
    src/ui/login_page.cpp
    src/ui/login_page.h
    src/ui/map_controller.cpp
    src/ui/map_controller.h
    src/ui/main_window.cpp
    src/ui/main_window.h
    src/ui/order_controller.cpp
    src/ui/order_controller.h
    src/ui/order_page.cpp
    src/ui/order_page.h
    src/ui/profile_controller.cpp
    src/ui/profile_controller.h
    src/ui/profile_page.cpp
    src/ui/profile_page.h
    src/ui/scan_controller.cpp
    src/ui/scan_controller.h
    src/ui/scan_page.cpp
    src/ui/scan_page.h
    src/ui/station_browser_controller.cpp
    src/ui/station_browser_controller.h
    src/ui/station_browser_page.cpp
    src/ui/station_browser_page.h
)

target_include_directories(charging_client_ui
    PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/src"
)

target_link_libraries(charging_client_ui
    PUBLIC
        charging_client_api
        Qt6::Network
        Qt6::Widgets
)

if(CHARGING_CLIENT_ENABLE_WEBENGINE)
    find_package(Qt6 6.2 REQUIRED COMPONENTS WebEngineWidgets)
    target_link_libraries(charging_client_ui PUBLIC Qt6::WebEngineWidgets)
    target_compile_definitions(
        charging_client_ui
        PUBLIC CHARGING_CLIENT_HAS_WEBENGINE=1
    )
endif()

add_executable(user-client
    src/main.cpp
)

target_link_libraries(user-client
    PRIVATE
        charging_client_ui
)

if(BUILD_TESTING)
    find_package(Qt6 6.2 REQUIRED COMPONENTS Test)

    add_executable(charging_client_assistant_tests tests/assistant_tests.cpp)
    target_link_libraries(charging_client_assistant_tests PRIVATE charging_client_ui Qt6::Test)
    target_compile_definitions(charging_client_assistant_tests PRIVATE
        ASSISTANT_FIXTURE_PATH="${CMAKE_CURRENT_SOURCE_DIR}/../contracts/examples/assistant-responses.local.json")
    add_test(NAME charging_client_assistant_tests COMMAND charging_client_assistant_tests)

    add_executable(charging_client_assistant_ui_tests tests/assistant_ui_tests.cpp)
    target_link_libraries(charging_client_assistant_ui_tests PRIVATE charging_client_ui Qt6::Test)
    add_test(NAME charging_client_assistant_ui_tests COMMAND charging_client_assistant_ui_tests)
    set_tests_properties(charging_client_assistant_ui_tests PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

    add_executable(charging_client_ui_recovery_tests tests/ui_recovery_tests.cpp)
    target_link_libraries(charging_client_ui_recovery_tests PRIVATE charging_client_ui Qt6::Test)
    add_test(NAME charging_client_ui_recovery_tests COMMAND charging_client_ui_recovery_tests)
    set_tests_properties(charging_client_ui_recovery_tests PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

    add_executable(charging_client_tests
        tests/main_window_tests.cpp
    )

    target_link_libraries(charging_client_tests
        PRIVATE
            charging_client_ui
            Qt6::Test
    )

    add_test(NAME charging_client_tests COMMAND charging_client_tests)
    set_tests_properties(charging_client_tests PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
    )

    add_executable(charging_client_api_tests
        tests/api_interface_tests.cpp
    )

    target_link_libraries(charging_client_api_tests
        PRIVATE
            charging_client_api
            Qt6::Test
    )

    add_test(NAME charging_client_api_tests COMMAND charging_client_api_tests)

    add_executable(charging_client_mock_api_tests
        tests/mock_charging_api_tests.cpp
    )

    target_link_libraries(charging_client_mock_api_tests
        PRIVATE
            charging_client_api
            Qt6::Test
    )

    add_test(NAME charging_client_mock_api_tests COMMAND charging_client_mock_api_tests)

    add_executable(charging_client_tcp_api_tests
        tests/tcp_charging_api_tests.cpp
    )

    target_link_libraries(charging_client_tcp_api_tests
        PRIVATE
            charging_client_api
            Qt6::Network
            Qt6::Test
    )

    add_test(NAME charging_client_tcp_api_tests COMMAND charging_client_tcp_api_tests)

    add_executable(charging_client_avatar_storage_tests
        tests/avatar_storage_tests.cpp
    )

    target_link_libraries(charging_client_avatar_storage_tests
        PRIVATE
            charging_client_ui
            Qt6::Test
    )

    add_test(
        NAME charging_client_avatar_storage_tests
        COMMAND charging_client_avatar_storage_tests
    )

    add_executable(charging_client_input_method_tests
        tests/input_method_setup_tests.cpp
    )

    target_link_libraries(charging_client_input_method_tests
        PRIVATE
            charging_client_ui
            Qt6::Test
    )

    add_test(
        NAME charging_client_input_method_tests
        COMMAND charging_client_input_method_tests
    )

    add_executable(charging_client_map_tests
        tests/mock_map_service_tests.cpp
    )

    target_link_libraries(charging_client_map_tests
        PRIVATE
            charging_client_ui
            Qt6::Test
    )

    add_test(NAME charging_client_map_tests COMMAND charging_client_map_tests)
endif()
