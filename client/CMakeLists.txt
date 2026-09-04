cmake_minimum_required(VERSION 3.22)

project(charging_user_client LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC OFF)

find_package(Qt6 6.2 REQUIRED COMPONENTS Widgets)

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
)

target_include_directories(charging_client_api
    PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/src"
)

target_link_libraries(charging_client_api
    PUBLIC
        charging_protocol
        Qt6::Core
)

add_library(charging_client_ui STATIC
    src/local/avatar_storage.cpp
    src/local/avatar_storage.h
    src/local/i_map_service.cpp
    src/local/i_map_service.h
    src/local/input_method_setup.cpp
    src/local/input_method_setup.h
    src/local/map_types.h
    src/local/mock_map_service.cpp
    src/local/mock_map_service.h
    src/ui/charging_stop_dialog.cpp
    src/ui/charging_stop_dialog.h
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
        Qt6::Widgets
)

add_executable(user-client
    src/main.cpp
)

target_link_libraries(user-client
    PRIVATE
        charging_client_ui
)

if(BUILD_TESTING)
    find_package(Qt6 6.2 REQUIRED COMPONENTS Test)

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
