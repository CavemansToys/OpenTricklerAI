cmake_minimum_required(VERSION 3.13)

# Handle PICO_SDK_PATH from both CMake variable and environment variable
if(DEFINED PICO_SDK_PATH)
    # CMake variable takes precedence (e.g. from -DPICO_SDK_PATH=...)
    set(PICO_SDK_PATH "${PICO_SDK_PATH}")
elseif(DEFINED ENV{PICO_SDK_PATH})
    # Fall back to environment variable
    set(PICO_SDK_PATH "$ENV{PICO_SDK_PATH}")
else()
    message(FATAL_ERROR "PICO_SDK_PATH not set. Please set it via -DPICO_SDK_PATH=... or environment variable")
endif()

include(${PICO_SDK_PATH}/external/pico_sdk_import.cmake)

project(pico2w_blink C CXX ASM)
set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

pico_sdk_init()

add_executable(blink
    blink.c
)

target_link_libraries(blink
    pico_stdlib
    pico_cyw43_arch_none
)

pico_add_extra_outputs(blink)

pico_enable_stdio_usb(blink 1)
pico_enable_stdio_uart(blink 0)
