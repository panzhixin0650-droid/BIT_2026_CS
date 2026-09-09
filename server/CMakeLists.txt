# 服务端构建脚本：编译管理端程序并注册各类测试目标
cmake_minimum_required(VERSION 3.22)

project(charging_server LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)

# 服务端需要网络、SQLite 与管理界面组件
find_package(Qt6 6.2 REQUIRED COMPONENTS Core Network Sql Widgets)

# 引入共享协议子项目，客户端与服务端共用一套报文定义
add_subdirectory(
    ${CMAKE_CURRENT_SOURCE_DIR}/../shared/protocol
    ${CMAKE_CURRENT_BINARY_DIR}/shared_protocol
)

# 服务端可执行文件：应用服务、传输、持久化、适配器与管理界面
add_executable(server-app
    src/main.cpp
    src/application/application_service.cpp
    src/application/order_service.cpp
    src/application/order_billing.h
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
    src/admin_ui/support_tickets_page.cpp
    src/admin_ui/support_tickets_page.h
    src/admin_ui/pile_status_chart.cpp
    src/admin_ui/pile_status_chart.h
    src/admin_ui/analysis_bar_chart.cpp
    src/admin_ui/revenue_chart.cpp
    src/admin_ui/revenue_chart.h
    resources/server_resources.qrc
)

target_include_directories(server-app
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)

# 链接共享协议库和所需 Qt 模块
target_link_libraries(server-app
    PRIVATE
        charging_protocol
        Qt6::Core
        Qt6::Network
        Qt6::Sql
        Qt6::Widgets
)

include(CTest)

# 开启测试时才构建测试目标，并要求 sqlite3 命令可用
if(BUILD_TESTING)
    find_package(Qt6 6.2 REQUIRED COMPONENTS Test)
    find_program(SQLITE3_EXECUTABLE sqlite3 REQUIRED)

    # 业务单元测试：用内存替身跑应用服务与路由
    add_executable(charging_server_tests
        tests/server_tests.cpp
        src/application/application_service.cpp
        src/application/order_service.cpp
        src/application/order_billing.h
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

    # 管理界面测试，运行时用 offscreen 平台避免依赖显示器
    add_executable(charging_admin_ui_tests
        tests/admin_ui_tests.cpp
        src/admin_ui/support_tickets_page.cpp
        src/admin_ui/admin_window.cpp
        src/admin_ui/admin_facade.cpp
        src/admin_ui/pile_status_chart.cpp
        src/admin_ui/analysis_bar_chart.cpp
        src/admin_ui/revenue_chart.cpp
        src/application/application_service.cpp
        src/application/order_service.cpp
        src/application/session_store.cpp
        src/persistence/in_memory_repository.cpp
        src/adapters/mock_pile.cpp
        src/adapters/mock_prediction_provider.cpp
        resources/server_resources.qrc
    )
    target_include_directories(charging_admin_ui_tests PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
    )
    target_link_libraries(charging_admin_ui_tests PRIVATE
        charging_protocol Qt6::Widgets Qt6::Test
    )
    add_test(NAME charging_admin_ui_tests COMMAND charging_admin_ui_tests)
    set_tests_properties(charging_admin_ui_tests PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
    )

    # 仓储测试单独编译，直连真实 SQLite 实现
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

    # 把迁移与种子 SQL 路径以宏形式传给测试代码
    target_compile_definitions(charging_repository_tests
        PRIVATE
            CHARGING_SQLITE3_EXECUTABLE="${SQLITE3_EXECUTABLE}"
            CHARGING_DATABASE_MIGRATION_PATH="${CMAKE_CURRENT_SOURCE_DIR}/../database/migrations/001_initial_demo.sql"
            CHARGING_TICKET_MIGRATION_PATH="${CMAKE_CURRENT_SOURCE_DIR}/../database/migrations/002_support_tickets.sql"
            CHARGING_ADMIN_MIGRATION_PATH="${CMAKE_CURRENT_SOURCE_DIR}/../database/migrations/003_admin_accounts.sql"
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

    # Exercise the actual client adapter against TcpGateway and SQLite, in
    # addition to the same business cases against the in-memory substitute.
    # 订单流程测试同时接入真实 TCP 网关与客户端适配器
    add_executable(charging_order_flow_tests
        tests/order_flow_tests.cpp
        src/application/application_service.cpp
        src/application/order_service.cpp
        src/application/session_store.cpp
        src/persistence/repository.cpp
        src/persistence/in_memory_repository.cpp
        src/adapters/mock_pile.cpp
        src/adapters/mock_prediction_provider.cpp
        src/transport/request_router.cpp
        src/transport/tcp_gateway.cpp
        ../client/src/api/i_charging_api.cpp
        ../client/src/api/tcp_charging_api.cpp
    )
    target_include_directories(charging_order_flow_tests PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
        ${CMAKE_CURRENT_SOURCE_DIR}/../client/src
    )
    # 预约超时与高峰计价用契约示例文件作为测试样本
    target_compile_definitions(charging_order_flow_tests PRIVATE
            CHARGING_RESERVATION_FIXTURE_PATH="${CMAKE_CURRENT_SOURCE_DIR}/../contracts/examples/reservation-timeout.local.json"
            CHARGING_PEAK_FIXTURE_PATH="${CMAKE_CURRENT_SOURCE_DIR}/../contracts/examples/demo-peak-pricing.local.json"
            CHARGING_SQLITE3_EXECUTABLE="${SQLITE3_EXECUTABLE}"
            CHARGING_DATABASE_MIGRATION_PATH="${CMAKE_CURRENT_SOURCE_DIR}/../database/migrations/001_initial_demo.sql"
            CHARGING_TICKET_MIGRATION_PATH="${CMAKE_CURRENT_SOURCE_DIR}/../database/migrations/002_support_tickets.sql"
            CHARGING_ADMIN_MIGRATION_PATH="${CMAKE_CURRENT_SOURCE_DIR}/../database/migrations/003_admin_accounts.sql"
            CHARGING_DATABASE_SEED_PATH="${CMAKE_CURRENT_SOURCE_DIR}/../database/seeds/demo.sql"
    )
    target_link_libraries(charging_order_flow_tests PRIVATE
        charging_protocol Qt6::Core Qt6::Network Qt6::Sql Qt6::Test
    )
    add_test(NAME charging_order_flow_tests COMMAND charging_order_flow_tests)

    # 工单流程测试覆盖客服工单相关迁移与接口
    add_executable(charging_support_ticket_flow_tests
        tests/support_ticket_flow_tests.cpp
        src/application/application_service.cpp
        src/application/order_service.cpp
        src/application/session_store.cpp
        src/persistence/repository.cpp
        src/persistence/in_memory_repository.cpp
        src/adapters/mock_pile.cpp
        src/adapters/mock_prediction_provider.cpp
        src/admin_ui/admin_facade.cpp
        src/transport/request_router.cpp
        src/transport/tcp_gateway.cpp
        ../client/src/api/i_charging_api.cpp
        ../client/src/api/tcp_charging_api.cpp
    )
    target_include_directories(charging_support_ticket_flow_tests PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src ${CMAKE_CURRENT_SOURCE_DIR}/../client/src)
    target_compile_definitions(charging_support_ticket_flow_tests PRIVATE
        CHARGING_SQLITE3_EXECUTABLE="${SQLITE3_EXECUTABLE}"
        CHARGING_DATABASE_MIGRATION_PATH="${CMAKE_CURRENT_SOURCE_DIR}/../database/migrations/001_initial_demo.sql"
        CHARGING_DATABASE_SEED_PATH="${CMAKE_CURRENT_SOURCE_DIR}/../database/seeds/demo.sql"
        CHARGING_TICKET_MIGRATION_PATH="${CMAKE_CURRENT_SOURCE_DIR}/../database/migrations/002_support_tickets.sql"
        CHARGING_ADMIN_MIGRATION_PATH="${CMAKE_CURRENT_SOURCE_DIR}/../database/migrations/003_admin_accounts.sql"
        CHARGING_REPAIR_MIGRATION_PATH="${CMAKE_CURRENT_SOURCE_DIR}/../database/migrations/004_repair_tickets.sql")
    target_link_libraries(charging_support_ticket_flow_tests PRIVATE
        charging_protocol Qt6::Core Qt6::Network Qt6::Sql Qt6::Test)
    add_test(NAME charging_support_ticket_flow_tests COMMAND charging_support_ticket_flow_tests)
endif()
