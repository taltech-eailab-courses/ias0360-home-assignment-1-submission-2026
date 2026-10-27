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
# ====================================================================================
cmake_minimum_required(VERSION 3.28)

set(PICO_BOARD pico_w CACHE STRING "Board type")
set(PROGRAM_NAME 500HZ_Reading)      # TODO: pick a target name — this is what your .uf2 will be called

include(pico_sdk_import.cmake)

project(${PROGRAM_NAME} C CXX ASM)

pico_sdk_init()

add_subdirectory(icm20948)
add_subdirectory(sd_card_driver)

add_executable(${PROGRAM_NAME}
    main.c
    buffer.c
    led.c
    sampling.c
    sensor.c
    storage.c
    config/hw_config.c
    stats.c
    features.c
)

target_compile_definitions(${PROGRAM_NAME} PRIVATE
  PICO_STACK_SIZE=0x1000
  PICO_CORE1_STACK_SIZE=0x800
)

target_compile_options(${PROGRAM_NAME} PUBLIC
  -Wall
  -Wextra
  -Wshadow
)

pico_enable_stdio_uart(${PROGRAM_NAME} 0)
pico_enable_stdio_usb(${PROGRAM_NAME} 1)

target_link_libraries(${PROGRAM_NAME}
    icm20948
    hardware_i2c
    pico_stdlib
    sd_custom_driver
    pico_multicore
    pico_cyw43_arch_none
)

pico_add_extra_outputs(${PROGRAM_NAME})