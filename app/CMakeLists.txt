add_executable(bbq20
	backlight.c
	debug.c
	fifo.c
	gpioexp.c
	puppet_i2c.c
	interrupt.c
	keyboard.c
	main.c
	reg.c
	touchpad.c
	usb.c
	usb_descriptors.c
)

add_compile_options(-Wall -Wextra -Wpedantic)

target_include_directories(bbq20 PRIVATE ${CMAKE_CURRENT_LIST_DIR})

target_link_libraries(bbq20
	cmsis_core
	hardware_i2c
	hardware_pwm
	pico_bootsel_via_double_reset
	pico_stdlib
	tinyusb_device
)

# create map/bin/hex/uf2 file in addition to elf
pico_add_extra_outputs(bbq20)
