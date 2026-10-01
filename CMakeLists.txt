# == DO NOT EDIT THE FOLLOWING LINES for the Raspberry Pi Pico VS Code Extension to work ==
if(WIN32)
    set(USERHOME $ENV{USERPROFILE})
else()
    set(USERHOME $ENV{HOME})
endif()

set(sdkVersion 2.2.0)
set(toolchainVersion 14_2_Rel1)
set(picotoolVersion 2.2.0)

set(picoVscode ${USERHOME}/.pico-sdk/cmake/pico-vscode.cmake)

if (EXISTS ${picoVscode})
    include(${picoVscode})
endif()
# ============================================================================

cmake_minimum_required(VERSION 3.13)

set(PICO_BOARD pico_w CACHE STRING "Board type")
set(PROGRAM_NAME handwriting)

include(pico_sdk_import.cmake)

project(${PROGRAM_NAME} C CXX ASM)

pico_sdk_init()


# Include paths needed by the Waveshare libraries
include_directories(
    ${CMAKE_CURRENT_LIST_DIR}
    ${CMAKE_CURRENT_LIST_DIR}/lib/config
    ${CMAKE_CURRENT_LIST_DIR}/lib/lcd
    ${CMAKE_CURRENT_LIST_DIR}/lib/font
)

# Waveshare libraries
add_subdirectory(lib/config)
add_subdirectory(lib/lcd)
add_subdirectory(lib/font)


# Our application
add_executable(${PROGRAM_NAME}
    main.c
    recognizer.c
)


# USB serial output
pico_enable_stdio_uart(${PROGRAM_NAME} 0)
pico_enable_stdio_usb(${PROGRAM_NAME} 1)


target_link_libraries(${PROGRAM_NAME}
    lcd
    font
    config

    pico_stdlib
    pico_multicore

    hardware_spi
    hardware_i2c
    hardware_adc
    hardware_pwm
    hardware_clocks
)


target_include_directories(${PROGRAM_NAME} PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}
)


pico_add_extra_outputs(${PROGRAM_NAME})