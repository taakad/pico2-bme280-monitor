cmake_minimum_required(VERSION 3.16)

include($ENV{PICO_SDK_PATH}/external/pico_sdk_import.cmake)

project(bme280_cpp C CXX ASM)
set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

pico_sdk_init()

# Raspberry Pi Pico 2 (RP2350). Override with -DPICO_BOARD=pico for RP2040.
if(NOT DEFINED PICO_BOARD)
    set(PICO_BOARD "pico2")
endif()

add_executable(bme280_monitor
    src/main.cpp
    src/bme280.cpp
    src/bme280_math.cpp
    src/i2c_scan.cpp
)

target_include_directories(bme280_monitor PRIVATE src)

target_link_libraries(bme280_monitor
    pico_stdlib
    hardware_i2c
    hardware_watchdog
)

# By default stdio_usb drops output produced before the host opens the CDC port,
# which hides the boot banner. Wait up to 3 s for the host to attach.
target_compile_definitions(bme280_monitor PRIVATE
    PICO_STDIO_USB_CONNECT_WAIT_TIMEOUT_MS=3000
)

# printf over USB CDC; no UART wiring is required on the Pico 2.
pico_enable_stdio_usb(bme280_monitor 1)
pico_enable_stdio_uart(bme280_monitor 0)

pico_add_extra_outputs(bme280_monitor)
