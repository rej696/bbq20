cmake_minimum_required(VERSION 3.13)

set(PICO_PLATFORM "rp2040")
set(PICO_BOARD_HEADER_DIRS ${CMAKE_CURRENT_LIST_DIR}/boards)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

include(3rdparty/pico-sdk/pico_sdk_init.cmake)

project(bbq20)

pico_sdk_init()

add_subdirectory(app)

# binary info in flash
pico_set_program_name(bbq20 "BBQ20")
pico_set_program_version(bbq20 "0.1")

# printf targets
#pico_enable_stdio_usb(bbq20 1)
pico_enable_stdio_uart(bbq20 1)
