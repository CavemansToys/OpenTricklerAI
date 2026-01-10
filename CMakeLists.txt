cmake_minimum_required(VERSION 3.25)

# Set project data
set(PROJECT_NAME "OpenTricklerController")
set(TARGET_NAME "app")

message("CMAKE_SOURCE_DIR: ${CMAKE_SOURCE_DIR}")

# Set env variable 'PICO_SDK_PATH' to the local Pico SDK
# Comment out the set() if you have a global copy of the
# SDK set and $PICO_SDK_PATH defined in your $PATH
set(ENV{PICO_SDK_PATH} "${CMAKE_SOURCE_DIR}/library/pico-sdk")
set(PICO_SDK_PATH $ENV{PICO_SDK_PATH})

# CRITICAL: Disable binary info BEFORE including SDK so SDK libraries respect it
# Use BOTH compile definitions AND options for maximum compatibility
# See: https://www.raspberrypi.com/documentation/pico-sdk/runtime.html
add_compile_definitions(PICO_NO_BINARY_INFO=1)
add_compile_options(-DPICO_NO_BINARY_INFO=1)

# Include the Pico SDK
include(${PICO_SDK_PATH}/pico_sdk_init.cmake)

# Name the project
project(${PROJECT_NAME}
        LANGUAGES C CXX ASM
        DESCRIPTION "Firmware for OpenTrickler Controller"
)

# Initialise the Pico SDK
pico_sdk_init()

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

# Set flags and directory variables
set(CMAKE_C_FLAGS_DEBUG "${CMAKE_C_FLAGS_DEBUG} -O0 -DDEBUG")
set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -O0 -DDEBUG")
set(SRC_DIRECTORY "${CMAKE_SOURCE_DIR}/src")
set(FREERTOS_SRC_DIRECTORY "${CMAKE_SOURCE_DIR}/library/FreeRTOS-Kernel")
set(U8G2_SRC_DIRECYTORY "${CMAKE_SOURCE_DIR}/library/u8g2")
set(U8G2_MUI_DIRECTORY "${U8G2_SRC_DIRECYTORY}/csrc")
set(TMC_SRC_DIRECTORY "${CMAKE_SOURCE_DIR}/library/Trinamic-library")
set(SCRIPTS_DIRECTORY "${CMAKE_SOURCE_DIR}/scripts")
set(PICO_BOARD_HEADER_DIRS "${CMAKE_SOURCE_DIR}/targets")

# Append compiler flags
SET(CMAKE_C_FLAGS  "${CMAKE_C_FLAGS} -fdata-sections -ffunction-sections -Wall")
SET(CMAKE_CXX_FLAGS  "${CMAKE_CXX_FLAGS} -fdata-sections -ffunction-sections -Wall")
# SET(CMAKE_EXE_LINKER_FLAGS  "${CMAKE_EXE_LINKER_FLAGS} -Wl,--gc-sections")

# RP2350 Enhanced: Enable FMA and hardware FPU optimizations
# Cortex-M33 has hardware FPU (6x faster) and fused multiply-add
# These flags take advantage of RP2350's superior floating-point hardware
SET(CMAKE_C_FLAGS  "${CMAKE_C_FLAGS} -ffp-contract=fast")
SET(CMAKE_CXX_FLAGS  "${CMAKE_CXX_FLAGS} -ffp-contract=fast")
message(STATUS "RP2350 Enhanced: FMA optimization enabled (-ffp-contract=fast)")

# RP2350 OTA: Embed partition table for A/B partition switching
# Uses ROM bootloader instead of custom bootloader
set(PICO_PARTITION_TABLE_JSON "${CMAKE_SOURCE_DIR}/partition_table.json")
if(EXISTS ${PICO_PARTITION_TABLE_JSON})
    message(STATUS "RP2350 OTA: Embedding partition table from ${PICO_PARTITION_TABLE_JSON}")
    set(PICO_EMBED_PARTITION_TABLE ON)
else()
    message(WARNING "RP2350 OTA: Partition table not found, OTA disabled")
endif()

#===============================================================================
# Dual-Bank OTA Configuration
#===============================================================================
option(BUILD_OTA_BOOTLOADER "Build OTA bootloader" ON)
option(USE_OTA_LINKER_SCRIPT "Use OTA application linker script (Bank A)" ON)
option(OTA_TEST_MODE "Build minimal OTA test firmware (no hardware peripherals)" OFF)

message("==============================================")
message("OTA Firmware Update Configuration")
message("==============================================")
message("BUILD_OTA_BOOTLOADER: ${BUILD_OTA_BOOTLOADER}")
message("USE_OTA_LINKER_SCRIPT: ${USE_OTA_LINKER_SCRIPT}")
message("OTA_TEST_MODE: ${OTA_TEST_MODE}")

# Build bootloader if requested
if(BUILD_OTA_BOOTLOADER)
    message(STATUS "Building OTA bootloader...")
    add_subdirectory(${SRC_DIRECTORY}/bootloader)
endif()

# Add OTA_TEST_MODE preprocessor definition if enabled
if(OTA_TEST_MODE)
    message(STATUS "OTA Test Mode ENABLED - Hardware peripherals will be disabled")
    add_compile_definitions(OTA_TEST_MODE)
endif()

message("==============================================")
#===============================================================================

# Application
add_executable("${TARGET_NAME}")

# Include source
include_directories(${SRC_DIRECTORY})

# Pull in FreeRTOS (RP2040 or RP2350 based on platform)
if(PICO_PLATFORM STREQUAL "rp2350" OR PICO_PLATFORM STREQUAL "rp2350-arm-s" OR PICO_PLATFORM STREQUAL "rp2350-riscv")
    include(${FREERTOS_SRC_DIRECTORY}/portable/ThirdParty/GCC/RP2350_ARM_NTZ/FreeRTOS_Kernel_import.cmake)
else()
    include(${FREERTOS_SRC_DIRECTORY}/portable/ThirdParty/GCC/RP2040/FreeRTOS_Kernel_import.cmake)
endif()

# Pull in u8g2
add_subdirectory(${U8G2_SRC_DIRECYTORY})

# Pull in Trinamic library (we don't use CMakeLists provided by TMC library as I don't want hal stuff)
add_library(trinamic INTERFACE)
target_sources(trinamic INTERFACE
    ${TMC_SRC_DIRECTORY}/common.c
    ${TMC_SRC_DIRECTORY}/tmc_interface.c
    ${TMC_SRC_DIRECTORY}/tmc2130.c
    ${TMC_SRC_DIRECTORY}/tmc2209.c
    ${TMC_SRC_DIRECTORY}/tmc5160.c
)
target_include_directories(trinamic INTERFACE ${TMC_SRC_DIRECTORY})



# Pull in u8g2 mui
add_library(u8g2_mui
    ${U8G2_MUI_DIRECTORY}/mui_u8g2.c
    ${U8G2_MUI_DIRECTORY}/mui.c
    ${U8G2_MUI_DIRECTORY}/u8g2_button.c
    ${U8G2_MUI_DIRECTORY}/u8g2_box.c
)
target_link_libraries(u8g2_mui u8g2)

# Pull in src
add_subdirectory(${SRC_DIRECTORY})

# Collect all source files
file(GLOB SRC ${SRC_DIRECTORY}/*.c 
              ${SRC_DIRECTORY}/*.cpp)

# Include application source file
target_sources("${TARGET_NAME}" PUBLIC
    ${SRC}
)

# Include additional headers
target_include_directories("${TARGET_NAME}" PUBLIC ${CMAKE_SOURCE_DIR}/targets)

# Include libraries1
target_link_libraries("${TARGET_NAME}"
    pico_stdlib
    hardware_pio
    hardware_spi
    hardware_i2c
    hardware_pwm
    FreeRTOS-Kernel
    FreeRTOS-Kernel-Heap4
    u8g2
    u8g2_mui
    trinamic
    app_version

    # Required by cyw43 wireless module
    pico_cyw43_arch_lwip_sys_freertos
    pico_lwip_freertos
    # pico_lwip_http
)

# Disable binary info for application (556KB binary is too large for binary info in first 256 bytes)
target_compile_definitions("${TARGET_NAME}" PRIVATE PICO_NO_BINARY_INFO=1)

target_link_options("${TARGET_NAME}" PUBLIC -Wl,--gc-sections -Wl,--print-memory-usage)
# set( CMAKE_VERBOSE_MAKEFILE on )

#===============================================================================
# OTA Linker Script Configuration
#===============================================================================
if(USE_OTA_LINKER_SCRIPT)
    message(STATUS "Using OTA application linker script (Bank A: 0x10006000)")
    pico_set_linker_script("${TARGET_NAME}" ${CMAKE_SOURCE_DIR}/application.ld)
else()
    message(STATUS "Using default Pico SDK linker script")
endif()
#===============================================================================

# Enable USB serial output for debugging
pico_enable_stdio_usb("${TARGET_NAME}" 1)
pico_enable_stdio_uart("${TARGET_NAME}" 0)
message(STATUS "USB serial debugging enabled")

# Generate extra outputs
pico_add_extra_outputs("${TARGET_NAME}")
