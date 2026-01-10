# Generate PIO headers
pico_generate_pio_header(app ${SRC_DIRECTORY}/ws2812.pio OUTPUT_DIR ${SRC_DIRECTORY}/generated)
# Generate PIO headers
pico_generate_pio_header(app ${SRC_DIRECTORY}/stepper.pio OUTPUT_DIR ${SRC_DIRECTORY}/generated)

# Find Python, as Python is required to convert pages into source
find_package (Python COMPONENTS Interpreter REQUIRED)

# Generate HTML header
# Reference: https://cliutils.gitlab.io/modern-cmake/chapters/basics/programs.html

# web_portal.html
add_custom_target(
    generate_web_portal_header ALL
    DEPENDS "${SRC_DIRECTORY}/generated/web_portal.html.h"
)

add_custom_command(
    OUTPUT "${SRC_DIRECTORY}/generated/web_portal.html.h"
    DEPENDS "${SRC_DIRECTORY}/html/web_portal.html"
    COMMAND "${Python_EXECUTABLE}" "${SCRIPTS_DIRECTORY}/html2header.py" -vv --no-minify -f ${SRC_DIRECTORY}/html/web_portal.html -o ${SRC_DIRECTORY}/generated/web_portal.html.h
    COMMENT "Generating web_portal.html header"
)

add_dependencies("${TARGET_NAME}" generate_web_portal_header)


# wizard.html
add_custom_target(
    generate_wizard_header ALL
    DEPENDS "${SRC_DIRECTORY}/generated/wizard.html.h"
)

add_custom_command(
    OUTPUT "${SRC_DIRECTORY}/generated/wizard.html.h"
    DEPENDS "${SRC_DIRECTORY}/html/wizard.html"
    COMMAND "${Python_EXECUTABLE}" "${SCRIPTS_DIRECTORY}/html2header.py" -vv --no-minify -f ${SRC_DIRECTORY}/html/wizard.html -o ${SRC_DIRECTORY}/generated/wizard.html.h
    COMMENT "Generating wizard.html header"
)

add_dependencies("${TARGET_NAME}" generate_wizard_header)


# display_mirror.html
add_custom_target(
    generate_display_mirror ALL
    DEPENDS "${SRC_DIRECTORY}/generated/display_mirror.html.h"
)

add_custom_command(
    OUTPUT "${SRC_DIRECTORY}/generated/display_mirror.html.h"
    DEPENDS "${SRC_DIRECTORY}/html/display_mirror.html"
    COMMAND "${Python_EXECUTABLE}" "${SCRIPTS_DIRECTORY}/html2header.py" -vv --no-minify -f ${SRC_DIRECTORY}/html/display_mirror.html -o ${SRC_DIRECTORY}/generated/display_mirror.html.h
    COMMENT "Generating display_mirror.html header"
)

add_dependencies("${TARGET_NAME}" generate_display_mirror)


# Generate version
MESSAGE("Python_EXECUTABLE: ${Python_EXECUTABLE}")
add_custom_command(
    OUTPUT 
        "${SRC_DIRECTORY}/generated/version.c"
        "${SRC_DIRECTORY}/generated/version.h"
        "non_exist_file"
    COMMAND "${Python_EXECUTABLE}" "${SCRIPTS_DIRECTORY}/gen_version.py" -o ${SRC_DIRECTORY}/generated --build-type="${CMAKE_BUILD_TYPE}"
    COMMENT "Generating version"
)
add_library(app_version "${SRC_DIRECTORY}/generated/version.c")


#===============================================================================
# OTA Firmware Update Module
#===============================================================================
message(STATUS "Adding OTA firmware update module sources")

# Add firmware update sources to application
target_sources(${TARGET_NAME} PRIVATE
    ${SRC_DIRECTORY}/firmware_update/crc32.c
    ${SRC_DIRECTORY}/firmware_update/flash_ops.c
    ${SRC_DIRECTORY}/firmware_update/firmware_manager.c
    ${SRC_DIRECTORY}/firmware_update/firmware_upload.c
    ${SRC_DIRECTORY}/firmware_update/firmware_download.c
    ${SRC_DIRECTORY}/firmware_update/rest_firmware.c
)

# Add bootloader metadata source (shared between bootloader and application)
target_sources(${TARGET_NAME} PRIVATE
    ${SRC_DIRECTORY}/bootloader/metadata.c
)

# Add include directories for firmware update module
target_include_directories(${TARGET_NAME} PRIVATE
    ${SRC_DIRECTORY}/firmware_update
    ${SRC_DIRECTORY}/bootloader
)

message(STATUS "OTA firmware update module added")
#===============================================================================


#===============================================================================
# AI PID Auto-Tuning Module
#===============================================================================
message(STATUS "Adding AI PID auto-tuning module sources")

# Add AI tuning source to application
target_sources(${TARGET_NAME} PRIVATE
    ${SRC_DIRECTORY}/ai_tuning.c
    ${SRC_DIRECTORY}/rest_ai_tuning.c
)

message(STATUS "AI PID auto-tuning module added")
#===============================================================================
