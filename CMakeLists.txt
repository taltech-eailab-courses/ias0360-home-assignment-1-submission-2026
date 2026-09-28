cmake_minimum_required(VERSION 3.13)

set(PICO_BOARD pico_w CACHE STRING "Board type")
set(PROGRAM_NAME image_processing)

include(../../lab_1_1/lcd_sd_card_example/pico_sdk_import.cmake)
project(${PROGRAM_NAME} C CXX ASM)

pico_sdk_init()

set(LAB_1_1_LCD ${CMAKE_CURRENT_LIST_DIR}/../../lab_1_1/lcd_sd_card_example)
add_subdirectory(${LAB_1_1_LCD}/lib/config ${CMAKE_BINARY_DIR}/lib_config)
add_subdirectory(${LAB_1_1_LCD}/lib/lcd ${CMAKE_BINARY_DIR}/lib_lcd)
add_subdirectory(${LAB_1_1_LCD}/lib/font ${CMAKE_BINARY_DIR}/lib_font)
add_subdirectory(${LAB_1_1_LCD}/lib/sd_card_driver ${CMAKE_BINARY_DIR}/lib_sd_card)

add_executable(${PROGRAM_NAME}
    ${LAB_1_1_LCD}/config/hw_config.c
    image_processing.cpp
)

target_include_directories(${PROGRAM_NAME} PRIVATE
    ${LAB_1_1_LCD}/lib/config
    ${LAB_1_1_LCD}/lib/lcd
    ${LAB_1_1_LCD}/lib/font
    ${LAB_1_1_LCD}/lib/sd_card_driver/include
    ${LAB_1_1_LCD}/lib/sd_card_driver/ff15/source
)

target_compile_definitions(${PROGRAM_NAME} PRIVATE
    PICO_STACK_SIZE=0x1000
    PICO_CORE1_STACK_SIZE=0x1000
    PARAM_ASSERTIONS_ENABLE_ALL=1
    PICO_MALLOC_PANIC=1
    PICO_USE_STACK_GUARDS=1
)

target_compile_options(${PROGRAM_NAME} PRIVATE -Wall -Wextra -Wshadow)

target_link_libraries(${PROGRAM_NAME}
    lcd
    font
    config
    pico_stdlib
    pico_multicore
    pico_util
    sd_custom_driver
    hardware_spi
    hardware_i2c
    hardware_pwm
)

pico_enable_stdio_uart(${PROGRAM_NAME} 0)
pico_enable_stdio_usb(${PROGRAM_NAME} 1)
pico_add_extra_outputs(${PROGRAM_NAME})
