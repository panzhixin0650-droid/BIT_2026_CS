cmake_minimum_required(VERSION 3.22)

project(charging_protocol LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_AUTOMOC ON)

find_package(Qt6 6.2 REQUIRED COMPONENTS Core)

add_library(charging_protocol STATIC
    src/dto.cpp
    src/envelope.cpp
    src/frame_codec.cpp
)

target_include_directories(charging_protocol
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(charging_protocol PUBLIC Qt6::Core)

include(CTest)

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

    target_compile_definitions(charging_protocol_tests
        PRIVATE
            CHARGING_PROTOCOL_FIXTURE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/../../contracts/examples"
    )

    add_test(NAME charging_protocol_tests COMMAND charging_protocol_tests)
endif()
