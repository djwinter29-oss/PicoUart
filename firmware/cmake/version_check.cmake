# Standalone `cmake -P` driver for firmware/cmake/version.cmake, used by
# host/python/tests/test_version_policy.py to exercise the version acceptance/
# rejection policy without configuring the full firmware/pico-sdk project
# (no toolchain or SDK checkout required).
#
# Usage: cmake -D PICO_UART_VERSION=1.2.3 -P version_check.cmake
# On success prints "RESULT major=<M> minor=<N> patch=<P> bcd=<BCD>".
# On policy violation, message(FATAL_ERROR ...) reports the reason on stderr
# and cmake exits non-zero.
get_filename_component(_version_check_dir "${CMAKE_CURRENT_LIST_DIR}" ABSOLUTE)
include(${_version_check_dir}/version.cmake)
message("RESULT major=${PICO_UART_VERSION_MAJOR} minor=${PICO_UART_VERSION_MINOR} patch=${PICO_UART_VERSION_PATCH} bcd=${PICO_UART_BCD_DEVICE}")
