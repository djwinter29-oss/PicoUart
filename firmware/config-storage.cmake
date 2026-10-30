# SPDX-License-Identifier: BSD-3-Clause
# Copyright (c) 2026 Ji Dong
# Use the SDK-resolved region, including board-header/default overrides, without changing physical capacity.
set(_sdk_flash_region "${CMAKE_BINARY_DIR}/pico-sdk/src/rp2_common/pico_standard_link/pico_flash_region.ld")
file(READ "${_sdk_flash_region}" _flash_region)
string(REGEX MATCH "LENGTH = ([^\n]+)" _flash_length "${_flash_region}")
if(NOT _flash_length)
    message(FATAL_ERROR "Cannot resolve board flash capacity from ${_sdk_flash_region}")
endif()
math(EXPR PICO_UART_CONFIG_FLASH_SIZE "${CMAKE_MATCH_1}")
math(EXPR PICO_UART_CONFIG_FLASH_OFFSET "${PICO_UART_CONFIG_FLASH_SIZE} - 4096")
math(EXPR _flash_alignment "${PICO_UART_CONFIG_FLASH_SIZE} % 4096")
if(PICO_UART_CONFIG_FLASH_SIZE LESS 8192 OR NOT _flash_alignment EQUAL 0)
    message(FATAL_ERROR "Persistent configuration requires a sector-aligned board flash capacity of at least 8192 bytes")
endif()
file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/config-link")
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/config-link/pico_flash_region.ld"
    "FLASH(rx) : ORIGIN = 0x10000000, LENGTH = ${PICO_UART_CONFIG_FLASH_OFFSET}\n")
target_link_options(pico_uart PRIVATE
    "LINKER:--defsym=__pico_uart_config_start=0x10000000+${PICO_UART_CONFIG_FLASH_OFFSET}"
    "LINKER:--defsym=__pico_uart_config_end=0x10000000+${PICO_UART_CONFIG_FLASH_SIZE}")
pico_add_linker_script_override_path(pico_uart "${CMAKE_CURRENT_BINARY_DIR}/config-link" FILES pico_flash_region.ld)
target_compile_definitions(pico_uart PRIVATE
    PICO_UART_CONFIG_FLASH_SIZE=${PICO_UART_CONFIG_FLASH_SIZE}u
    PICO_UART_CONFIG_FLASH_OFFSET=${PICO_UART_CONFIG_FLASH_OFFSET}u)
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/config-layout.json"
    "{\"version\":1,\"flash_size\":${PICO_UART_CONFIG_FLASH_SIZE},"
    "\"sector_offset\":${PICO_UART_CONFIG_FLASH_OFFSET},\"sector_size\":4096,"
    "\"target_family\":\"${PICO_UART_TARGET_FAMILY}\"}\n")
