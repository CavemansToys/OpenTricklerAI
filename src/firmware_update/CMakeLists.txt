# Firmware Update Module Build Configuration

# This module provides OTA firmware update functionality for the application

# Firmware update source files
set(FIRMWARE_UPDATE_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/crc32.c
    ${CMAKE_CURRENT_SOURCE_DIR}/flash_ops.c
)

# Firmware update header files (for reference)
set(FIRMWARE_UPDATE_HEADERS
    ${CMAKE_CURRENT_SOURCE_DIR}/crc32.h
    ${CMAKE_CURRENT_SOURCE_DIR}/flash_ops.h
)

# Add to parent target (will be linked into application)
# This is a module, not a standalone library
target_sources(${TARGET_NAME} PRIVATE
    ${FIRMWARE_UPDATE_SOURCES}
)

# Add include directories
target_include_directories(${TARGET_NAME} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/../bootloader
)

# Add dependencies
target_link_libraries(${TARGET_NAME}
    hardware_flash
    hardware_sync
    hardware_watchdog
)

message(STATUS "Firmware update module configured")
message(STATUS "  - Sources: crc32.c, flash_ops.c")
