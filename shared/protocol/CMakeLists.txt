# 构建共享协议静态库 charging_protocol 及其单元测试
cmake_minimum_required(VERSION 3.22)

project(charging_protocol LANGUAGES CXX)

# 统一使用 C++17 标准并开启 Qt 元对象自动处理
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_AUTOMOC ON)

find_package(Qt6 6.2 REQUIRED COMPONENTS Core)

# 库源码：数据结构、工单、信封、分帧编解码
add_library(charging_protocol STATIC
    src/dto.cpp
    src/support_ticket.cpp
    src/envelope.cpp
    src/frame_codec.cpp
)

# 对外公开 include 目录，客户端与服务端共用同一份协议
target_include_directories(charging_protocol
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(charging_protocol PUBLIC Qt6::Core)

include(CTest)

# 仅在开启测试时构建协议测试可执行文件
if(BUILD_TESTING)
    find_package(Qt6 6.2 REQUIRED COMPONENTS Test)

    add_executable(charging_protocol_tests
        tests/protocol_tests.cpp
    )

    target_link_libraries(charging_protocol_tests
        PRIVATE
            charging_protocol
            Qt6::Test
    )

    # 用宏把契约样例目录传给测试，便于读取示例 JSON
    target_compile_definitions(charging_protocol_tests
        PRIVATE
            CHARGING_PROTOCOL_FIXTURE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/../../contracts/examples"
    )

    add_test(NAME charging_protocol_tests COMMAND charging_protocol_tests)
endif()
