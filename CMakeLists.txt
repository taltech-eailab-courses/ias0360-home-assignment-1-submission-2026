cmake_minimum_required(VERSION 3.13)

set(PICO_BOARD pico_w CACHE STRING "Board type")

# Include Pico SDK macros
include(pico_sdk_import.cmake)

project(home_assignment_1 C CXX ASM)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

# Initialize Pico SDK
pico_sdk_init()

add_subdirectory(lib/config)
add_subdirectory(lib/font)
add_subdirectory(lib/lcd)
add_subdirectory(lib/sd_card_driver build_sd)

include_directories(
    include
    config
    lib/config
    lib/font
    lib/lcd
    lib/sd_card_driver/include
    lib/sd_card_driver/ff15/source
)

add_executable(home_assignment_1
    config/hw_config.c
    src/main.c
    src/sobel.c
    src/quantization.c
    src/statistic.c
)

target_compile_definitions(home_assignment_1 PRIVATE
    PICO_STACK_SIZE=0x1000
    PICO_CORE1_STACK_SIZE=0x1000
)

target_compile_options(home_assignment_1 PRIVATE
    -Wall
    -Wextra
)

target_link_libraries(home_assignment_1
    pico_stdlib
    pico_multicore
    pico_util
    hardware_i2c
    hardware_spi
    hardware_dma
    hardware_pio
    hardware_clocks
    hardware_adc
    sd_custom_driver
    m
    config
    font
    lcd
)

target_link_options(home_assignment_1 PRIVATE
    "-Wl,--print-memory-usage"
)

# Enable USB stdio for serial console and report data export
pico_enable_stdio_usb(home_assignment_1 1)
pico_enable_stdio_uart(home_assignment_1 0)

pico_add_extra_outputs(home_assignment_1)
