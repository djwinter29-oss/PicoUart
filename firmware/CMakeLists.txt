cmake_minimum_required(VERSION 3.13)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

set(PICO_BOARD pico CACHE STRING "Target board")
set(PICO_UART_VERSION "0.0.0-dev" CACHE STRING "Firmware semantic version string")
set(PICO_UART_SYSTEM_CLOCK_KHZ "" CACHE STRING "Optional system clock target override in kHz")

# Tag form is v1.2.3; CMake receives 1.2.3 (pre-release suffixes like -dev / -rc1 are allowed).
if(NOT PICO_UART_VERSION MATCHES "^([0-9]+)\\.([0-9]+)\\.([0-9]+)")
    message(FATAL_ERROR
        "PICO_UART_VERSION must start with MAJOR.MINOR.PATCH (got '${PICO_UART_VERSION}')")
endif()
set(PICO_UART_VERSION_MAJOR "${CMAKE_MATCH_1}")
set(PICO_UART_VERSION_MINOR "${CMAKE_MATCH_2}")
set(PICO_UART_VERSION_PATCH "${CMAKE_MATCH_3}")
if(PICO_UART_VERSION_MAJOR GREATER 99 OR PICO_UART_VERSION_MINOR GREATER 99)
    message(FATAL_ERROR
        "PICO_UART_VERSION major/minor must be 0-99 for USB bcdDevice BCD encoding "
        "(got ${PICO_UART_VERSION_MAJOR}.${PICO_UART_VERSION_MINOR})")
endif()
# USB bcdDevice advertises major.minor only (1.2.3 -> 0x0102, shown as 1.02 / 1.2).
math(EXPR PICO_UART_BCD_DEVICE
    "((${PICO_UART_VERSION_MAJOR} / 10) * 4096) + ((${PICO_UART_VERSION_MAJOR} % 10) * 256) + ((${PICO_UART_VERSION_MINOR} / 10) * 16) + (${PICO_UART_VERSION_MINOR} % 10)")

if(DEFINED PICO_SDK_PATH)
    include(${PICO_SDK_PATH}/external/pico_sdk_import.cmake)
elseif(DEFINED ENV{PICO_SDK_PATH})
    include($ENV{PICO_SDK_PATH}/external/pico_sdk_import.cmake)
else()
    message(FATAL_ERROR "Set PICO_SDK_PATH to your pico-sdk checkout before configuring firmware.")
endif()

project(pico_uart C CXX ASM)

pico_sdk_init()

add_executable(pico_uart
    src/driver/led.c
    src/driver/system.c
    src/driver/temperature.c
    src/main.c
    src/uart/hw/driver.c
    src/uart/line_coding.c
    src/uart/pio/driver.c
    src/uart/ring_buffer/ring_buffer.c
    src/uart/uart_driver.c
    src/usb/usb_cdc.c
    src/usb/usb_hid.c
    src/usb/usb_descriptors.c
)

pico_generate_pio_header(pico_uart ${CMAKE_CURRENT_LIST_DIR}/src/uart/pio/uart.pio)

target_include_directories(pico_uart PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}/src
    ${CMAKE_CURRENT_LIST_DIR}/src/config
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
)

if (NOT PICO_UART_SYSTEM_CLOCK_KHZ STREQUAL "")
    target_compile_definitions(pico_uart PRIVATE PICO_UART_SYSTEM_CLOCK_KHZ=${PICO_UART_SYSTEM_CLOCK_KHZ}u)
endif ()

pico_enable_stdio_uart(pico_uart 0)
pico_enable_stdio_usb(pico_uart 0)

pico_add_extra_outputs(pico_uart)