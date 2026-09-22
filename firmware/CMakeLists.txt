cmake_minimum_required(VERSION 3.13)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

set(PICO_BOARD pico CACHE STRING "Target board")
set(PICO_UART_VERSION "0.0.0-dev" CACHE STRING "Firmware semantic version string")
set(PICO_UART_SYSTEM_CLOCK_KHZ "" CACHE STRING "Optional system clock target override in kHz")
option(PICO_UART_ALLOW_HID_RESET "Enable the trusted-host HID watchdog reset command" OFF)
option(PICO_UART_ALLOW_UNSAFE_OVERCLOCK "Allow a system clock above the target's rated default" OFF)

# Parses PICO_UART_VERSION into PICO_UART_VERSION_{MAJOR,MINOR,PATCH} and
# PICO_UART_BCD_DEVICE; see firmware/version.cmake for the policy and
# firmware/tests/test_version_policy.py for standalone `cmake -P` coverage.
include(${CMAKE_CURRENT_LIST_DIR}/version.cmake)

if(NOT PICO_UART_SYSTEM_CLOCK_KHZ STREQUAL "")
    if(NOT PICO_UART_SYSTEM_CLOCK_KHZ MATCHES "^[1-9][0-9]*$")
        message(FATAL_ERROR "PICO_UART_SYSTEM_CLOCK_KHZ must be a positive integer in kHz")
    endif()
    string(LENGTH "${PICO_UART_SYSTEM_CLOCK_KHZ}" _pico_uart_clock_length)
    if(_pico_uart_clock_length GREATER 6 OR PICO_UART_SYSTEM_CLOCK_KHZ GREATER 400000)
        message(FATAL_ERROR
            "PICO_UART_SYSTEM_CLOCK_KHZ must be no greater than 400000 kHz "
            "(got ${PICO_UART_SYSTEM_CLOCK_KHZ})")
    endif()
endif()

if(DEFINED PICO_SDK_PATH)
    include(${PICO_SDK_PATH}/external/pico_sdk_import.cmake)
elseif(DEFINED ENV{PICO_SDK_PATH})
    include($ENV{PICO_SDK_PATH}/external/pico_sdk_import.cmake)
else()
    message(FATAL_ERROR "Set PICO_SDK_PATH to your pico-sdk checkout before configuring firmware.")
endif()

project(pico_uart C CXX ASM)

pico_sdk_init()

if(PICO_BOARD MATCHES "^(pico2|rp2350)")
    set(PICO_UART_RATED_SYSTEM_CLOCK_KHZ 150000)
else()
    set(PICO_UART_RATED_SYSTEM_CLOCK_KHZ 125000)
endif()
if(PICO_UART_SYSTEM_CLOCK_KHZ STREQUAL "")
    set(PICO_UART_SYSTEM_CLOCK_KHZ "${PICO_UART_RATED_SYSTEM_CLOCK_KHZ}" CACHE STRING
        "Optional system clock target override in kHz" FORCE)
endif()
if(NOT PICO_UART_ALLOW_UNSAFE_OVERCLOCK AND
   NOT PICO_UART_SYSTEM_CLOCK_KHZ EQUAL PICO_UART_RATED_SYSTEM_CLOCK_KHZ)
    message(FATAL_ERROR
        "PICO_UART_SYSTEM_CLOCK_KHZ=${PICO_UART_SYSTEM_CLOCK_KHZ} exceeds the supported build contract for ${PICO_BOARD}; "
        "use the rated ${PICO_UART_RATED_SYSTEM_CLOCK_KHZ} kHz clock or explicitly set PICO_UART_ALLOW_UNSAFE_OVERCLOCK=ON")
endif()

add_executable(pico_uart)

target_sources(pico_uart PRIVATE
    src/board/uart_board.c
    src/board/uart_topology.c
    src/driver/led.c
    src/driver/system.c
    src/driver/temperature.c
    src/main.c
    src/uart/backend.c
    src/uart/hw/dma_claim.c
    src/uart/hw/dma_claim_ops.c
    src/uart/hw/hw_uart_driver.c
    src/uart/line_coding.c
    src/uart/pio/pio_uart_driver.c
    src/uart/pio/resource_claim.c
    src/uart/pio/resource_claim_ops.c
    src/uart/ring_buffer/ring_buffer.c
    src/uart/uart_driver.c
    src/usb/usb_cdc.c
    src/usb/usb_hid.c
    src/usb/usb_descriptors.c
)

pico_generate_pio_header(pico_uart ${CMAKE_CURRENT_LIST_DIR}/src/uart/pio/uart.pio)

target_include_directories(pico_uart PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}/src
)

target_link_libraries(pico_uart
    pico_stdlib
    pico_multicore
    hardware_adc
    hardware_clocks
    hardware_dma
    hardware_pio
    hardware_uart
    pico_unique_id
    tinyusb_device
)

pico_set_program_name(pico_uart "PicoUart")
pico_set_program_description(pico_uart "USB CDC + HID multi-UART bridge")
pico_set_program_version(pico_uart "${PICO_UART_VERSION}")
target_compile_definitions(pico_uart PRIVATE
    PICO_UART_VERSION_MAJOR=${PICO_UART_VERSION_MAJOR}u
    PICO_UART_VERSION_MINOR=${PICO_UART_VERSION_MINOR}u
    PICO_UART_VERSION_PATCH=${PICO_UART_VERSION_PATCH}u
    PICO_UART_BCD_DEVICE=${PICO_UART_BCD_DEVICE}u
    PICO_UART_ALLOW_HID_RESET=$<BOOL:${PICO_UART_ALLOW_HID_RESET}>
    PICO_UART_SYSTEM_CLOCK_KHZ=${PICO_UART_SYSTEM_CLOCK_KHZ}u
)

if(CMAKE_C_COMPILER_ID STREQUAL "GNU")
    target_compile_options(pico_uart PRIVATE -Wall -Wextra -Wformat=2)
endif()

if(PICO_BOARD MATCHES "^pico$")
    # RP2040 silicon can miss the first SET_ADDRESS after a bus reset, so the host
    # re-enumerates the device (seen here as the HID/CDC interfaces dropping and
    # reappearing). TinyUSB ships a workaround (rp2040_usb_device_enumeration_fix())
    # but leaves it opt-in; enable it only for RP2040 boards, where the errata applies.
    target_compile_definitions(pico_uart PRIVATE PICO_RP2040_USB_DEVICE_ENUMERATION_FIX=1)
endif()

pico_enable_stdio_uart(pico_uart 0)
pico_enable_stdio_usb(pico_uart 0)

pico_add_extra_outputs(pico_uart)
