cmake_minimum_required(VERSION 3.13)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

set(PICO_BOARD pico CACHE STRING "Target board")
set(PICO_UART_SYSTEM_CLOCK_KHZ "" CACHE STRING "Optional system clock target override in kHz")

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

if (NOT PICO_UART_SYSTEM_CLOCK_KHZ STREQUAL "")
    target_compile_definitions(pico_uart PRIVATE PICO_UART_SYSTEM_CLOCK_KHZ=${PICO_UART_SYSTEM_CLOCK_KHZ}u)
endif ()

pico_enable_stdio_uart(pico_uart 0)
pico_enable_stdio_usb(pico_uart 0)

pico_add_extra_outputs(pico_uart)