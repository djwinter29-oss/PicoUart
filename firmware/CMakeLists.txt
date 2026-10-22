cmake_minimum_required(VERSION 3.13)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

set(PICO_BOARD pico CACHE STRING "Target board")
set(PICO_UART_VERSION "0.0.0-dev" CACHE STRING "Firmware semantic version string")
option(PICO_UART_ALLOW_HID_RESET "Enable the HID watchdog reset command (arm, then reset within 2 s)" ON)
if((DEFINED PICO_UART_SYSTEM_CLOCK_KHZ AND NOT PICO_UART_SYSTEM_CLOCK_KHZ STREQUAL "") OR
   PICO_UART_ALLOW_UNSAFE_OVERCLOCK)
    message(FATAL_ERROR "Build-time clock overrides were removed; use tools/firmware/config.py to select a stored profile")
endif()

# pico_sdk_import.cmake is designed to be include()d prior to project(); doing so
# here (rather than after the clock-ceiling checks below) also lets the SDK's own
# board-header processing populate PICO_PLATFORM before we need it.
if(DEFINED PICO_SDK_PATH)
    include(${PICO_SDK_PATH}/external/pico_sdk_import.cmake)
elseif(DEFINED ENV{PICO_SDK_PATH})
    include($ENV{PICO_SDK_PATH}/external/pico_sdk_import.cmake)
else()
    message(FATAL_ERROR "Set PICO_SDK_PATH to your pico-sdk checkout before configuring firmware.")
endif()

# Target-family discrimination (RP2040 vs RP2350) drives the clock ceiling/rated
# defaults below. PICO_PLATFORM is the SDK's own board metadata (populated from
# the board header's `pico_board_cmake_set(PICO_PLATFORM, ...)` declaration, e.g.
# "rp2350"/"rp2350-arm-s"/"rp2350-riscv" or "rp2040") during pico_sdk_import's
# preload, which already ran above, before project(). This covers any RP2350 or
# RP2040 board regardless of its name; a board name is never consulted, so a
# custom board cannot silently fall through to the wrong clock policy. Fail
# closed if the SDK did not resolve PICO_PLATFORM to a recognized family.
if(PICO_PLATFORM MATCHES "^rp2350")
    set(PICO_UART_TARGET_FAMILY rp2350)
elseif(PICO_PLATFORM MATCHES "^rp2040")
    set(PICO_UART_TARGET_FAMILY rp2040)
else()
    message(FATAL_ERROR
        "Unable to determine target family for PICO_BOARD='${PICO_BOARD}': "
        "PICO_PLATFORM='${PICO_PLATFORM}' is not a recognized rp2040/rp2350 platform.")
endif()

# Parses PICO_UART_VERSION into PICO_UART_VERSION_{MAJOR,MINOR,PATCH} and
# PICO_UART_BCD_DEVICE; see firmware/version.cmake for the policy and
# tests/firmware/test_version_policy.py for standalone `cmake -P` coverage.
include(${CMAKE_CURRENT_LIST_DIR}/version.cmake)

project(pico_uart C CXX ASM)

pico_sdk_init()

add_executable(pico_uart)

target_sources(pico_uart PRIVATE
    src/config/uart_config.c
    src/platform/boot_config.c
    src/platform/led.c
    src/platform/service_load.c
    src/platform/system.c
    src/platform/temperature.c
    src/main.c
    src/uart/bridge.c
    src/uart/control/mailbox.c
    src/uart/control/plane.c
    src/uart/hw/dma_claim.c
    src/uart/hw/dma_claim_ops.c
    src/uart/hw/hw_uart_backend.c
    src/uart/hw/hw_uart_board_config.c
    src/uart/hw/hw_uart_driver.c
    src/uart/line_coding.c
    src/uart/port_api.c
    src/uart/pio/pio_uart_backend.c
    src/uart/pio/pio_uart_board_config.c
    src/uart/pio/pio_uart_driver.c
    src/uart/pio/resource_claim.c
    src/uart/pio/resource_claim_ops.c
    src/uart/ring_buffer/ring_buffer.c
    src/uart/uart_driver.c
    src/uart/worker.c
    src/usb/usb_cdc.c
    src/usb/usb_hid.c
    src/usb/usb_descriptors.c
)

pico_generate_pio_header(pico_uart ${CMAKE_CURRENT_LIST_DIR}/src/uart/pio/uart.pio)
include(${CMAKE_CURRENT_LIST_DIR}/config-storage.cmake)

target_include_directories(pico_uart PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}/src
    ${CMAKE_CURRENT_LIST_DIR}/src/config
)

target_link_libraries(pico_uart
    pico_stdlib
    pico_multicore
    pico_flash
    hardware_flash
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
    # hardware/clocks.h only defaults SYS_CLK_VREG_VOLTAGE_AUTO_ADJUST to 1 when
    # compiled with PICO_RP2040 && SYS_CLK_HZ==200MHz (XOSC 12MHz, refdiv 1);
    # this project never defines SYS_CLK_HZ, so that default is already 0 for
    # every board/clock this CMakeLists builds, including the SDK's pre-main()
    # runtime_init_clocks.c startup source compiled into this target. Setting
    # it here is a defensive pin, not a behavior change, against that narrow
    # compile-time case being introduced later.
    SYS_CLK_VREG_VOLTAGE_AUTO_ADJUST=0
    PICO_UART_VERSION_MAJOR=${PICO_UART_VERSION_MAJOR}u
    PICO_UART_VERSION_MINOR=${PICO_UART_VERSION_MINOR}u
    PICO_UART_VERSION_PATCH=${PICO_UART_VERSION_PATCH}u
    PICO_UART_BCD_DEVICE=${PICO_UART_BCD_DEVICE}u
    PICO_UART_ALLOW_HID_RESET=$<BOOL:${PICO_UART_ALLOW_HID_RESET}>
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
