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

option(CHARGING_CLIENT_ENABLE_SCANNER
       "Enable camera and image QR scanning (Qt Multimedia and ZBar)"
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
    resources/album_resources.qrc
    src/ui/photo_album_page.cpp
    src/ui/photo_album_page.h
    resources/decorations.qrc
    src/ui/decorative_heading.cpp
    src/ui/decorative_heading.h
    resources/assistant_resources.qrc
    resources/map_resources.qrc
    src/assistant/assistant_config.cpp
    src/assistant/assistant_config.h
    src/assistant/assistant_service.cpp
    src/assistant/assistant_service.h
    src/assistant/assistant_request_worker.cpp
    src/assistant/assistant_request_worker.h
    src/ui/busy_indicator.cpp
    src/ui/busy_indicator.h
    src/assistant/knowledge_base.cpp
    src/assistant/knowledge_base.h
    src/ui/support_page.cpp
    src/ui/support_page.h
    src/ui/support_desk_page.cpp
    src/ui/support_desk_page.h
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
    src/ui/charging_page.cpp
    src/ui/charging_page.h
    src/ui/charging_controller.cpp
    src/ui/charging_controller.h
    src/ui/charging_art.cpp
    src/ui/charging_art.h
    src/ui/login_controller.cpp
    src/ui/login_controller.h
    src/ui/login_page.cpp
    src/ui/login_page.h
    src/ui/map_controller.cpp
    src/ui/map_controller.h
    src/ui/route_map_view.cpp
    src/ui/route_map_view.h
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
    src/ui/scan_page.cpp
    src/ui/scan_page.h
    src/ui/station_browser_controller.cpp
    src/ui/station_browser_controller.h
    src/ui/station_browser_page.cpp
    src/ui/station_browser_home.cpp
    src/ui/station_browser_discovery.cpp
    src/ui/station_browser_page.h
    src/ui/station_map_view.cpp
    src/ui/station_map_view.h
    src/ui/demo_map_backdrop.cpp
    src/ui/demo_map_backdrop.h
    src/ui/station_preview_card.cpp
    src/ui/station_preview_card.h
)

target_include_directories(charging_client_ui
    PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/src"
)

target_compile_definitions(charging_client_ui PRIVATE
    CHARGING_DEMO_ALBUM_DIR="${CMAKE_CURRENT_SOURCE_DIR}/demo-album")

target_link_libraries(charging_client_ui
    PUBLIC
        charging_client_api
        Qt6::Network
        Qt6::Widgets
)

if(CHARGING_CLIENT_ENABLE_WEBENGINE)
    find_package(Qt6 6.2 REQUIRED COMPONENTS WebEngineWidgets WebChannel)
    target_link_libraries(charging_client_ui PUBLIC Qt6::WebEngineWidgets Qt6::WebChannel)
    target_compile_definitions(
        charging_client_ui
        PUBLIC CHARGING_CLIENT_HAS_WEBENGINE=1
    )
endif()

if(CHARGING_CLIENT_ENABLE_SCANNER)
    find_package(Qt6 6.2 REQUIRED COMPONENTS Multimedia)
    find_path(ZBAR_INCLUDE_DIR NAMES zbar.h REQUIRED)
    find_library(ZBAR_LIBRARY NAMES zbar REQUIRED)
    target_sources(charging_client_ui PRIVATE
        src/local/qr_decoder.cpp src/local/qr_decoder.h
        src/ui/qr_scan_dialog.cpp src/ui/qr_scan_dialog.h)
    target_include_directories(charging_client_ui PRIVATE ${ZBAR_INCLUDE_DIR})
    target_link_libraries(charging_client_ui PUBLIC Qt6::Multimedia PRIVATE ${ZBAR_LIBRARY})
    target_compile_definitions(charging_client_ui PUBLIC CHARGING_CLIENT_HAS_SCANNER=1)
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

    if(CHARGING_CLIENT_ENABLE_SCANNER)
        add_executable(charging_client_qr_tests tests/qr_scanner_tests.cpp)
        target_link_libraries(charging_client_qr_tests PRIVATE charging_client_ui Qt6::Test)
        target_compile_definitions(charging_client_qr_tests PRIVATE
            QR_FIXTURE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/qr")
        add_test(NAME charging_client_qr_tests COMMAND charging_client_qr_tests)
        set_tests_properties(charging_client_qr_tests PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
    endif()

    add_executable(charging_client_decoration_tests tests/decoration_tests.cpp)
    target_link_libraries(charging_client_decoration_tests PRIVATE charging_client_ui Qt6::Test)
    add_test(NAME charging_client_decoration_tests COMMAND charging_client_decoration_tests)
    set_tests_properties(charging_client_decoration_tests PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

    add_executable(charging_client_station_map_tests tests/station_map_tests.cpp)
    target_link_libraries(charging_client_station_map_tests PRIVATE charging_client_ui Qt6::Test)
    add_test(NAME charging_client_station_map_tests COMMAND charging_client_station_map_tests)
    set_tests_properties(charging_client_station_map_tests PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

    add_executable(charging_client_assistant_responsiveness_tests tests/assistant_responsiveness_tests.cpp)
    target_link_libraries(charging_client_assistant_responsiveness_tests PRIVATE charging_client_ui Qt6::Test)
    add_test(NAME charging_client_assistant_responsiveness_tests COMMAND charging_client_assistant_responsiveness_tests)
    set_tests_properties(charging_client_assistant_responsiveness_tests PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 30)

    add_executable(charging_client_support_desk_tests tests/support_desk_tests.cpp)
    target_link_libraries(charging_client_support_desk_tests PRIVATE charging_client_ui Qt6::Test)
    add_test(NAME charging_client_support_desk_tests COMMAND charging_client_support_desk_tests)
    set_tests_properties(charging_client_support_desk_tests PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

    add_executable(charging_client_navigation_tests tests/navigation_tests.cpp)
    target_link_libraries(charging_client_navigation_tests PRIVATE charging_client_ui Qt6::Test)
    add_test(NAME charging_client_navigation_tests COMMAND charging_client_navigation_tests)
    # The real WebEngine renderer needs the desktop/Xvfb platform. Qt 6.2's
    # offscreen software scene graph can crash while replacing a map view.
    if(NOT CHARGING_CLIENT_ENABLE_WEBENGINE)
        set_tests_properties(charging_client_navigation_tests PROPERTIES
            ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
    endif()

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

    add_executable(charging_client_tcp_settlement_ui_tests tests/tcp_settlement_ui_tests.cpp)
    target_link_libraries(charging_client_tcp_settlement_ui_tests PRIVATE
        charging_client_ui Qt6::Test)
    target_compile_definitions(charging_client_tcp_settlement_ui_tests PRIVATE
        SETTLEMENT_FIXTURE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/../contracts/examples")
    add_test(NAME charging_client_tcp_settlement_ui_tests
        COMMAND charging_client_tcp_settlement_ui_tests)
    set_tests_properties(charging_client_tcp_settlement_ui_tests PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

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
