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
    src/ui/login_controller.cpp
    src/ui/login_controller.h
    src/ui/login_page.cpp
    src/ui/login_page.h
    src/ui/main_window.cpp
    src/ui/main_window.h
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
endif()
