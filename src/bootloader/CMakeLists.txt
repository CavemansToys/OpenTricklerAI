# Dual-Bank OTA Bootloader Build Configuration

# Bootloader target
set(BOOTLOADER_TARGET "bootloader")

# Bootloader source files
set(BOOTLOADER_SOURCES
    bootloader.c
    metadata.c
    ../firmware_update/crc32.c
    ../firmware_update/flash_ops.c
)

# Create bootloader executable
add_executable(${BOOTLOADER_TARGET} ${BOOTLOADER_SOURCES})

# Disable binary info FIRST to save space (bootloader is size-constrained)
target_compile_definitions(${BOOTLOADER_TARGET} PRIVATE PICO_NO_BINARY_INFO=1)

# Set C/C++ standards
set_target_properties(${BOOTLOADER_TARGET} PROPERTIES
    C_STANDARD 11
    CXX_STANDARD 17
)

# Include directories
target_include_directories(${BOOTLOADER_TARGET} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/..
    ${CMAKE_CURRENT_SOURCE_DIR}/../firmware_update
)

# Link libraries (minimal set, excluding pico_stdlib to avoid binary info)
target_link_libraries(${BOOTLOADER_TARGET}
    pico_platform
    pico_runtime
    pico_printf
    pico_stdio
    pico_stdio_usb
    hardware_flash
    hardware_sync
    hardware_watchdog
    hardware_gpio
    hardware_uart
    hardware_timer
)

# Use custom linker script
pico_set_linker_script(${BOOTLOADER_TARGET} ${CMAKE_CURRENT_SOURCE_DIR}/bootloader.ld)

# Enable USB output for debugging (optional - can be disabled for production)
pico_enable_stdio_usb(${BOOTLOADER_TARGET} 1)
pico_enable_stdio_uart(${BOOTLOADER_TARGET} 0)

# Generate extra output formats (bin, hex, uf2)
pico_add_extra_outputs(${BOOTLOADER_TARGET})

# Compiler optimizations
target_compile_options(${BOOTLOADER_TARGET} PRIVATE
    -Wall
    -Wextra
    -Os                    # Optimize for size
    -ffunction-sections
    -fdata-sections
)

# Linker options
target_link_options(${BOOTLOADER_TARGET} PRIVATE
    -Wl,--gc-sections      # Remove unused sections
    -Wl,--print-memory-usage
)

# NOTE: Binary info disabled via PICO_NO_BINARY_INFO=1 to save space
# Bootloader must fit in first 256 bytes of flash

message(STATUS "Bootloader build configured")
message(STATUS "  - Linker script: ${CMAKE_CURRENT_SOURCE_DIR}/bootloader.ld")
message(STATUS "  - Flash origin: 0x10000100 (~16KB)")
