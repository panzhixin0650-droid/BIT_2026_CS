cmake_minimum_required(VERSION 3.22)

project(charging_server LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)

find_package(Qt6 6.2 REQUIRED COMPONENTS Core Network Sql Widgets)

add_subdirectory(
    ${CMAKE_CURRENT_SOURCE_DIR}/../shared/protocol
    ${CMAKE_CURRENT_BINARY_DIR}/shared_protocol
)

add_executable(server-app
    src/main.cpp
    src/application/application_service.cpp
    src/application/application_service.h
    src/application/service_result.h
    src/application/session_store.cpp
    src/application/session_store.h
    src/transport/request_router.cpp
    src/transport/request_router.h
    src/transport/tcp_gateway.cpp
    src/transport/tcp_gateway.h
    src/persistence/repository.cpp
    src/persistence/repository.h
    src/persistence/i_repository.h
    src/persistence/in_memory_repository.cpp
    src/persistence/in_memory_repository.h
    src/adapters/mock_pile.cpp
    src/adapters/mock_pile.h
    src/adapters/mock_prediction_provider.cpp
    src/adapters/mock_prediction_provider.h
    src/adapters/dashboard_exporter.cpp
    src/adapters/dashboard_exporter.h
    src/admin_ui/admin_facade.cpp
    src/admin_ui/admin_facade.h
    src/admin_ui/admin_window.cpp
    src/admin_ui/admin_window.h
    src/admin_ui/pile_status_chart.cpp
    src/admin_ui/pile_status_chart.h
    src/admin_ui/revenue_chart.cpp
    src/admin_ui/revenue_chart.h
)

target_include_directories(server-app
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(server-app
    PRIVATE
        charging_protocol
        Qt6::Core
        Qt6::Network
        Qt6::Sql
        Qt6::Widgets
)

include(CTest)

if(BUILD_TESTING)
    find_package(Qt6 6.2 REQUIRED COMPONENTS Test)
    find_program(SQLITE3_EXECUTABLE sqlite3 REQUIRED)

    add_executable(charging_server_tests
        tests/server_tests.cpp
        src/application/application_service.cpp
        src/application/application_service.h
        src/application/service_result.h
        src/application/session_store.cpp
        src/application/session_store.h
        src/transport/request_router.cpp
        src/transport/request_router.h
        src/persistence/in_memory_repository.cpp
        src/persistence/in_memory_repository.h
        src/persistence/i_repository.h
        src/adapters/mock_pile.cpp
        src/adapters/mock_pile.h
        src/adapters/mock_prediction_provider.cpp
        src/adapters/mock_prediction_provider.h
        src/adapters/dashboard_exporter.cpp
        src/adapters/dashboard_exporter.h
        src/admin_ui/admin_facade.cpp
        src/admin_ui/admin_facade.h
    )

    target_include_directories(charging_server_tests
        PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/src
    )

    target_link_libraries(charging_server_tests
        PRIVATE
            charging_protocol
            Qt6::Core
            Qt6::Test
    )

    add_test(NAME charging_server_tests COMMAND charging_server_tests)

    add_executable(charging_repository_tests
        tests/repository_tests.cpp
        src/persistence/repository.cpp
        src/persistence/repository.h
        src/persistence/i_repository.h
    )

    target_include_directories(charging_repository_tests
        PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/src
    )

    target_compile_definitions(charging_repository_tests
        PRIVATE
            CHARGING_SQLITE3_EXECUTABLE="${SQLITE3_EXECUTABLE}"
            CHARGING_DATABASE_MIGRATION_PATH="${CMAKE_CURRENT_SOURCE_DIR}/../database/migrations/001_initial_demo.sql"
            CHARGING_DATABASE_SEED_PATH="${CMAKE_CURRENT_SOURCE_DIR}/../database/seeds/demo.sql"
    )

    target_link_libraries(charging_repository_tests
        PRIVATE
            charging_protocol
            Qt6::Core
            Qt6::Sql
            Qt6::Test
    )

    add_test(NAME charging_repository_tests COMMAND charging_repository_tests)
endif()
