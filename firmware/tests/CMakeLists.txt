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

add_library(pico_sdk_hooks STATIC stubs/pico_sdk_hooks.c)

set(PICO_UART_HOST_TEST_TARGETS)

function(pico_uart_add_unity_test target test_name)
    add_executable(${target} ${ARGN})
    target_include_directories(${target} PRIVATE
        ${FIRMWARE_ROOT}/src
    )
    target_link_libraries(${target} PRIVATE unity pico_sdk_hooks)
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

pico_uart_add_unity_test(test_bridge bridge
    test_bridge.c
    ${FIRMWARE_ROOT}/src/uart/bridge.c
    ${FIRMWARE_ROOT}/src/uart/ring_buffer/ring_buffer.c
)
target_include_directories(test_bridge PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/stubs
)

pico_uart_add_unity_test(test_worker worker
    test_worker.c
    ${FIRMWARE_ROOT}/src/uart/worker.c
    ${FIRMWARE_ROOT}/src/platform/service_load.c
)
target_include_directories(test_worker PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/stubs
)

pico_uart_add_unity_test(test_control_mailbox mailbox
    test_control_mailbox.c
    ${FIRMWARE_ROOT}/src/uart/control/mailbox.c
)
target_include_directories(test_control_mailbox PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/stubs
)

pico_uart_add_unity_test(test_control_plane plane
    test_control_plane.c
    ${FIRMWARE_ROOT}/src/uart/control/mailbox.c
    ${FIRMWARE_ROOT}/src/uart/control/plane.c
    ${FIRMWARE_ROOT}/src/uart/line_coding.c
    ${FIRMWARE_ROOT}/src/platform/service_load.c
)
target_include_directories(test_control_plane PRIVATE
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

# GNU ld drops unreferenced sections. Apple ld rejects --gc-sections; -dead_strip is its equivalent.
if(APPLE)
    set(_pico_uart_gc_sections -Wl,-dead_strip)
else()
    set(_pico_uart_gc_sections -Wl,--gc-sections)
endif()

set(_pio_tx_stubs "${CMAKE_CURRENT_BINARY_DIR}/pio_tx_stubs")
foreach(_header IN ITEMS
    hardware/pio.h hardware/clocks.h hardware/dma.h hardware/gpio.h hardware/irq.h
    hardware/regs/pio.h hardware/structs/dma.h hardware/sync.h
    pico/platform.h pico/stdlib.h pico/time.h uart.pio.h
)
    configure_file(support/pio_tx_sdk.h "${_pio_tx_stubs}/${_header}" COPYONLY)
endforeach()
pico_uart_add_unity_test(test_pio_tx_driver pio_tx_driver
    test_pio_tx_driver.c
    ${FIRMWARE_ROOT}/src/uart/line_coding.c
    ${FIRMWARE_ROOT}/src/uart/ring_buffer/ring_buffer.c
)
target_include_directories(test_pio_tx_driver BEFORE PRIVATE "${_pio_tx_stubs}")
target_compile_options(test_pio_tx_driver PRIVATE -ffunction-sections -fdata-sections)
target_link_options(test_pio_tx_driver PRIVATE ${_pico_uart_gc_sections})

set(_hw_uart_stubs "${CMAKE_CURRENT_BINARY_DIR}/hw_uart_stubs")
foreach(_header IN ITEMS
    hardware/uart.h hardware/clocks.h hardware/dma.h hardware/gpio.h hardware/irq.h
    hardware/regs/uart.h hardware/structs/dma.h hardware/sync.h pico/stdlib.h pico/time.h
)
    configure_file(support/hw_uart_sdk.h "${_hw_uart_stubs}/${_header}" COPYONLY)
endforeach()
pico_uart_add_unity_test(test_hw_uart_driver hw_uart_driver
    test_hw_uart_driver.c
    ${FIRMWARE_ROOT}/src/uart/ring_buffer/ring_buffer.c
    ${FIRMWARE_ROOT}/src/uart/hw/dma_claim.c
)
target_include_directories(test_hw_uart_driver BEFORE PRIVATE
    "${_hw_uart_stubs}" "${CMAKE_CURRENT_SOURCE_DIR}/support"
)
target_compile_options(test_hw_uart_driver PRIVATE -ffunction-sections -fdata-sections)
target_link_options(test_hw_uart_driver PRIVATE ${_pico_uart_gc_sections})

if(MINGW)
    # COFF unwind tables retain untested SDK paths despite --gc-sections.
    # These C harnesses need only the driver sections exercised by their mocks.
    target_compile_options(test_pio_tx_driver PRIVATE -fno-asynchronous-unwind-tables -fno-unwind-tables)
    target_compile_options(test_hw_uart_driver PRIVATE -fno-asynchronous-unwind-tables -fno-unwind-tables)
endif()

pico_uart_add_unity_test(test_cdc_soft_pending cdc_soft_pending
    test_cdc_soft_pending.c
)

pico_uart_add_unity_test(test_usb_cdc usb_cdc
    test_usb_cdc.c
    ${FIRMWARE_ROOT}/src/usb/usb_cdc.c
    ${FIRMWARE_ROOT}/src/uart/line_coding.c
    ${FIRMWARE_ROOT}/src/uart/bridge.c
    ${FIRMWARE_ROOT}/src/uart/ring_buffer/ring_buffer.c
)
target_include_directories(test_usb_cdc PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/stubs)

pico_uart_add_unity_test(test_topology topology
    test_topology.c
    ${CMAKE_CURRENT_SOURCE_DIR}/support/board/uart_topology.c
)
target_include_directories(test_topology PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/support)

pico_uart_add_unity_test(test_uart_board_config uart_board_config
    test_uart_board_config.c
    ${FIRMWARE_ROOT}/src/config/uart_config.c
    ${FIRMWARE_ROOT}/src/uart/hw/hw_uart_board_config.c
    ${FIRMWARE_ROOT}/src/uart/pio/pio_uart_board_config.c
    ${CMAKE_CURRENT_SOURCE_DIR}/support/board/uart_topology.c
)
target_include_directories(test_uart_board_config PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/stubs
    ${CMAKE_CURRENT_SOURCE_DIR}/support
)

pico_uart_add_unity_test(test_backend_policy backend_policy
    test_backend_policy.c
)

pico_uart_add_unity_test(test_backend_contract backend_contract
    test_backend_contract.c
)
target_include_directories(test_backend_contract PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/stubs
)

pico_uart_add_unity_test(test_native_backends native_backends
    test_native_backends.c
    ${FIRMWARE_ROOT}/src/config/uart_config.c
    ${FIRMWARE_ROOT}/src/uart/hw/hw_uart_board_config.c
    ${FIRMWARE_ROOT}/src/uart/pio/pio_uart_board_config.c
    ${FIRMWARE_ROOT}/src/uart/hw/hw_uart_backend.c
    ${FIRMWARE_ROOT}/src/uart/pio/pio_uart_backend.c
    ${FIRMWARE_ROOT}/src/uart/line_coding.c
)
target_include_directories(test_native_backends PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/stubs)

pico_uart_add_unity_test(test_hw_baud_rate hw_baud_rate
    test_hw_baud_rate.c
)

foreach(_led IN ITEMS present absent)
    pico_uart_add_unity_test(test_led_${_led} led_${_led}
        test_led.c
        ${FIRMWARE_ROOT}/src/platform/led.c
    )
    target_include_directories(test_led_${_led} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/stubs)
endforeach()
target_compile_definitions(test_led_present PRIVATE PICO_DEFAULT_LED_PIN=25)

pico_uart_add_unity_test(test_temperature temperature
    test_temperature.c
)

foreach(_family IN ITEMS rp2040 rp2350)
    pico_uart_add_unity_test(test_boot_config_${_family} boot_config_${_family} test_boot_config.c)
    target_include_directories(test_boot_config_${_family} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/stubs)
    target_compile_definitions(test_boot_config_${_family} PRIVATE
        PICO_RP2350=$<STREQUAL:${_family},rp2350>
        PICO_FLASH_SIZE_BYTES=4096
        PICO_UART_CONFIG_FLASH_SIZE=4096
        PICO_UART_CONFIG_FLASH_OFFSET=0
    )
endforeach()

pico_uart_add_unity_test(test_service_load service_load
    test_service_load.c
    ${FIRMWARE_ROOT}/src/platform/service_load.c
)
target_compile_definitions(test_service_load PRIVATE PICO_UART_PROFILE_TIMING=1)

foreach(_family IN ITEMS rp2040 rp2350 rp2040_no_reset)
    pico_uart_add_unity_test(test_usb_hid_${_family} usb_hid_${_family}
        test_usb_hid.c
        ${FIRMWARE_ROOT}/src/platform/service_load.c
    )
    target_include_directories(test_usb_hid_${_family} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/stubs)
    target_compile_definitions(test_usb_hid_${_family} PRIVATE
        PICO_RP2350=$<STREQUAL:${_family},rp2350>
        PICO_UART_BOARD_MODEL=$<IF:$<STREQUAL:${_family},rp2350>,3,1>
        PICO_UART_ALLOW_HID_RESET=$<NOT:$<STREQUAL:${_family},rp2040_no_reset>>
    )
    target_compile_options(test_usb_hid_${_family} PRIVATE -ffunction-sections -fdata-sections)
    target_link_options(test_usb_hid_${_family} PRIVATE ${_pico_uart_gc_sections})
    if(NOT APPLE)
        # Non-PIE lets the ELF harness take addresses of the absolute .set symbols.
        target_compile_options(test_usb_hid_${_family} PRIVATE -fno-pie)
        target_link_options(test_usb_hid_${_family} PRIVATE -no-pie)
    endif()
endforeach()

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

# Public-facade contract test. The real uart_driver.c is linked with host Pico
# stubs so invalid-port and pre-initialization guards remain testable without
# pretending to validate physical DMA or multicore startup on the host.
pico_uart_add_unity_test(test_uart_driver_facade uart_driver_facade
    test_uart_driver_facade.c
    ${FIRMWARE_ROOT}/src/uart/uart_driver.c
    ${FIRMWARE_ROOT}/src/uart/bridge.c
    ${FIRMWARE_ROOT}/src/uart/control/mailbox.c
    ${FIRMWARE_ROOT}/src/uart/line_coding.c
    ${FIRMWARE_ROOT}/src/uart/ring_buffer/ring_buffer.c
)
target_include_directories(test_uart_driver_facade PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/stubs
)
target_compile_options(test_uart_driver_facade PRIVATE -ffunction-sections -fdata-sections)
target_link_options(test_uart_driver_facade PRIVATE ${_pico_uart_gc_sections})

pico_uart_add_unity_test(test_cdc_uart_integration cdc_uart_integration
    test_uart_driver_facade.c
    ${FIRMWARE_ROOT}/src/usb/usb_cdc.c
    ${FIRMWARE_ROOT}/src/uart/uart_driver.c
    ${FIRMWARE_ROOT}/src/uart/control/plane.c
    ${FIRMWARE_ROOT}/src/uart/control/mailbox.c
    ${FIRMWARE_ROOT}/src/uart/bridge.c
    ${FIRMWARE_ROOT}/src/uart/line_coding.c
    ${FIRMWARE_ROOT}/src/uart/ring_buffer/ring_buffer.c
    ${FIRMWARE_ROOT}/src/platform/service_load.c
)
target_compile_definitions(test_cdc_uart_integration PRIVATE PICO_UART_CDC_INTEGRATION=1)
target_include_directories(test_cdc_uart_integration PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/stubs)

if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    set(_pico_uart_host_warnings -Wall -Wextra -Werror)
    foreach(_pico_uart_host_target IN LISTS PICO_UART_HOST_TEST_TARGETS)
        target_compile_options(${_pico_uart_host_target} PRIVATE ${_pico_uart_host_warnings})
    endforeach()
endif()
