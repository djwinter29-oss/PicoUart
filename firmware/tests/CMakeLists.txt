cmake_minimum_required(VERSION 3.16)

project(pico_uart_host_tests C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

enable_testing()

get_filename_component(FIRMWARE_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/.." ABSOLUTE)
get_filename_component(REPO_ROOT "${FIRMWARE_ROOT}/.." ABSOLUTE)

add_library(unity STATIC
    third_party/unity/unity.c
)
target_include_directories(unity PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/unity
)

set(PICO_UART_HOST_TEST_TARGETS)

function(pico_uart_add_unity_test target test_name)
    add_executable(${target} ${ARGN})
    target_include_directories(${target} PRIVATE
        ${FIRMWARE_ROOT}/src
    )
    target_link_libraries(${target} PRIVATE unity)
    add_test(NAME ${test_name} COMMAND ${target})
    list(APPEND PICO_UART_HOST_TEST_TARGETS ${target})
    set(PICO_UART_HOST_TEST_TARGETS ${PICO_UART_HOST_TEST_TARGETS} PARENT_SCOPE)
endfunction()

pico_uart_add_unity_test(test_ring_buffer ring_buffer
    test_ring_buffer.c
    ${FIRMWARE_ROOT}/src/uart/ring_buffer/ring_buffer.c
)
target_include_directories(test_ring_buffer PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/stubs
)

pico_uart_add_unity_test(test_line_coding line_coding
    test_line_coding.c
    ${FIRMWARE_ROOT}/src/uart/line_coding.c
)

pico_uart_add_unity_test(test_dma_progress dma_progress
    test_dma_progress.c
)

pico_uart_add_unity_test(test_txstall_wait txstall_wait
    test_txstall_wait.c
)

pico_uart_add_unity_test(test_cdc_soft_pending cdc_soft_pending
    test_cdc_soft_pending.c
)

pico_uart_add_unity_test(test_topology topology
    test_topology.c
    ${FIRMWARE_ROOT}/src/board/uart_topology.c
)

pico_uart_add_unity_test(test_backend_policy backend_policy
    test_backend_policy.c
)

pico_uart_add_unity_test(test_hw_baud_rate hw_baud_rate
    test_hw_baud_rate.c
)

pico_uart_add_unity_test(test_led_policy led_policy
    test_led_policy.c
)

pico_uart_add_unity_test(test_temperature temperature
    test_temperature.c
)

# Fault-injection test: HW UART DMA claim/rollback via injectable ops seam.
# Links the real dma_claim.c (production claim algorithm, no SDK deps) with a
# fake ops table that controls claim success/failure and logs every call.
pico_uart_add_unity_test(test_dma_claim dma_claim
    test_dma_claim.c
    ${FIRMWARE_ROOT}/src/uart/hw/dma_claim.c
)

# Fault-injection test: PIO state-machine/DMA claim rollback via injectable
# ops seam. Links the real resource_claim.c algorithm without SDK headers.
pico_uart_add_unity_test(test_resource_claim resource_claim
    test_resource_claim.c
    ${FIRMWARE_ROOT}/src/uart/pio/resource_claim.c
)

if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    set(_pico_uart_host_warnings -Wall -Wextra -Werror)
    foreach(_pico_uart_host_target IN LISTS PICO_UART_HOST_TEST_TARGETS)
        target_compile_options(${_pico_uart_host_target} PRIVATE ${_pico_uart_host_warnings})
    endforeach()
endif()
