cmake_minimum_required(VERSION 3.13)

set(PICO_BOARD pico2_w CACHE STRING "Board type")
set(PROGRAM_NAME imu)

include(pico_sdk_import.cmake)

project(${PROGRAM_NAME} C CXX ASM)

pico_sdk_init()

add_subdirectory(icm20948)
add_subdirectory(sd_card_driver build)

add_executable(${PROGRAM_NAME}
    main.c
    config/hw_config.c
    dsp_features.c
)

pico_enable_stdio_usb(${PROGRAM_NAME} 1)
pico_enable_stdio_uart(${PROGRAM_NAME} 0)

pico_add_extra_outputs(${PROGRAM_NAME})

target_include_directories(${PROGRAM_NAME} PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}
    ${CMAKE_CURRENT_LIST_DIR}/icm20948
)

target_link_libraries(${PROGRAM_NAME}
    pico_stdlib
    pico_cyw43_arch_none
    icm20948
    hardware_i2c
    pico_multicore
    pico_util
    sd_custom_driver
    hardware_clocks
    hardware_pio
    hardware_dma
    m
)
