cmake_minimum_required(VERSION 3.13)
set(PICO_BOARD pico_w CACHE STRING "Board type")
include(pico_sdk_import.cmake)
project(hw1 C CXX ASM)
pico_sdk_init()

add_subdirectory(lib/config)
add_subdirectory(lib/lcd)
add_subdirectory(lib/font)
add_subdirectory(lib/sd_card_driver build)

add_executable(hw1
    main.c
    preprocess.c
    filter.c
    quantize.c
    stats.c
    fft.c
    config/hw_config.c
)

# Which recording to process. The file must already be on the SD card.
#   cmake -DDATASET=walking ..
set(DATASET "still" CACHE STRING "Base name of the .bin recording to analyse")
set(N_SAMPLES 2048 CACHE STRING "Samples to load from the recording (power of two)")
set(FC_HZ 5.0 CACHE STRING "Low-pass cutoff for the analysed axis, Hz")
target_compile_definitions(hw1 PRIVATE
    DATASET="${DATASET}"
    N_SAMPLES=${N_SAMPLES}
    FC_HZ=${FC_HZ})

include_directories(. ./lib/config ./lib/lcd ./lib/font)

target_link_libraries(hw1
    pico_stdlib hardware_i2c pico_multicore
    lcd font config sd_custom_driver
    pico_util hardware_clocks hardware_adc hardware_pio
)

pico_enable_stdio_usb(hw1 1)
pico_enable_stdio_uart(hw1 0)
pico_add_extra_outputs(hw1)
