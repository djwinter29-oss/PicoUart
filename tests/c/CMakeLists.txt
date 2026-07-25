cmake_minimum_required(VERSION 3.16)

project(pico_uart_host_tests C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

enable_testing()

get_filename_component(REPO_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../.." ABSOLUTE)

add_library(unity STATIC
    third_party/unity/unity.c
)
target_include_directories(unity PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/unity
)

add_executable(test_ring_buffer
    test_ring_buffer.c
    ${REPO_ROOT}/firmware/src/uart/ring_buffer/ring_buffer.c
)
target_include_directories(test_ring_buffer PRIVATE
    ${REPO_ROOT}/firmware/src
    ${CMAKE_CURRENT_SOURCE_DIR}/stubs
)
target_link_libraries(test_ring_buffer PRIVATE unity)
add_test(NAME ring_buffer COMMAND test_ring_buffer)
