# Parse and validate PICO_UART_VERSION into PICO_UART_VERSION_{MAJOR,MINOR,PATCH}
# and compute PICO_UART_BCD_DEVICE. Extracted from CMakeLists.txt so the
# version policy can be exercised standalone with `cmake -P` (no Pico SDK or
# toolchain needed) — see host/python/tests/test_version_policy.py.

# Tag form is v1.2.3; CMake receives 1.2.3. Local builds may use -dev or -rcN.
if(NOT PICO_UART_VERSION MATCHES "^(0|[1-9][0-9]*)\\.(0|[1-9][0-9]*)\\.(0|[1-9][0-9]*)(-(dev|rc[1-9][0-9]*))?$")
    message(FATAL_ERROR
        "PICO_UART_VERSION must be MAJOR.MINOR.PATCH with optional -dev or -rcN "
        "(for example 1.2.3-rc1; got '${PICO_UART_VERSION}')")
endif()
set(PICO_UART_VERSION_MAJOR "${CMAKE_MATCH_1}")
set(PICO_UART_VERSION_MINOR "${CMAKE_MATCH_2}")
set(PICO_UART_VERSION_PATCH "${CMAKE_MATCH_3}")
foreach(_pico_uart_version_part MAJOR MINOR PATCH)
    set(_pico_uart_version_value "${PICO_UART_VERSION_${_pico_uart_version_part}}")
    string(LENGTH "${_pico_uart_version_value}" _pico_uart_version_length)
    if(_pico_uart_version_length GREATER 3 OR _pico_uart_version_value GREATER 255)
        message(FATAL_ERROR
            "PICO_UART_VERSION ${_pico_uart_version_part} must be 0-255 "
            "(got ${_pico_uart_version_value})")
    endif()
endforeach()

# USB bcdDevice can only encode major/minor 0-99 in BCD. When either exceeds
# 99, local builds remain valid but use 0x0000; release.yml rejects those tags.
if(PICO_UART_VERSION_MAJOR GREATER 99 OR PICO_UART_VERSION_MINOR GREATER 99)
    message(WARNING "PICO_UART_VERSION ${PICO_UART_VERSION_MAJOR}.${PICO_UART_VERSION_MINOR}.${PICO_UART_VERSION_PATCH} has major/minor > 99: USB bcdDevice will be 0x0000 (no meaningful BCD value). This build is not releasable as a tag; use it for local development only.")
    set(PICO_UART_BCD_DEVICE 0)
else()
    # USB bcdDevice advertises major.minor only (1.2.3 -> 0x0102).
    math(EXPR PICO_UART_BCD_DEVICE
        "((${PICO_UART_VERSION_MAJOR} / 10) * 4096) + ((${PICO_UART_VERSION_MAJOR} % 10) * 256) + ((${PICO_UART_VERSION_MINOR} / 10) * 16) + (${PICO_UART_VERSION_MINOR} % 10)")
endif()

if(CMAKE_SCRIPT_MODE_FILE)
    message("RESULT major=${PICO_UART_VERSION_MAJOR} minor=${PICO_UART_VERSION_MINOR} patch=${PICO_UART_VERSION_PATCH} bcd=${PICO_UART_BCD_DEVICE}")
endif()