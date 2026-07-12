cmake_minimum_required(VERSION 3.13)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

set(PICO_BOARD pico CACHE STRING "Target board")

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
    src/driver/hw_uart_driver.c
    src/driver/pio_uart_driver.c
    src/driver/uart_driver.c
    src/main.c
    src/usb/usb_cdc.c
    src/usb/usb_hid.c
    src/usb/usb_descriptors.c
)

target_include_directories(pico_uart PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}/src
)

target_link_libraries(pico_uart
    pico_stdlib
    hardware_dma
    hardware_pio
    hardware_uart
    pico_unique_id
    tinyusb_device
)

pico_enable_stdio_uart(pico_uart 0)
pico_enable_stdio_usb(pico_uart 0)

pico_add_extra_outputs(pico_uart)