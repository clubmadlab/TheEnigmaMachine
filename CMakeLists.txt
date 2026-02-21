cmake_minimum_required(VERSION 3.12)

include(pico_sdk_import.cmake)

set(PROJECT_NAME "Enigma")
project(${PROJECT_NAME} C CXX ASM)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

set(CMAKE_C_USE_RESPONSE_FILE_FOR_OBJECTS 0)
set(CMAKE_CXX_USE_RESPONSE_FILE_FOR_OBJECTS 0)
set(CMAKE_ASM_USE_RESPONSE_FILE_FOR_OBJECTS 0)

if (PICO_SDK_VERSION_STRING VERSION_LESS "1.4.0")
	message(FATAL_ERROR "Raspberry Pi Pico SDK version 1.4.0 (or later) required. Your version is ${PICO_SDK_VERSION_STRING}")
endif()

set(ENIGMA_PATH ${PROJECT_SOURCE_DIR})

pico_sdk_init()

add_compile_options(
	-Wall
	-Wno-format
	-Wno-unused-function
	-Wno-maybe-uninitialized
	-Ofast
)

add_executable(${PROJECT_NAME}
	main.c
	enigma.c
	keyboard.c
	lcd.c
	font.c
	graphics/enigma.c
	graphics/logo.c
	graphics/A.c
	graphics/B.c
	graphics/C.c
	graphics/D.c
	graphics/E.c
	graphics/F.c
	graphics/G.c
	graphics/H.c
	graphics/I.c
	graphics/J.c
	graphics/K.c
	graphics/L.c
	graphics/M.c
	graphics/N.c
	graphics/O.c
	graphics/P.c
	graphics/Q.c
	graphics/R.c
	graphics/S.c
	graphics/T.c
	graphics/U.c
	graphics/V.c
	graphics/W.c
	graphics/X.c
	graphics/Y.c
	graphics/Z.c
)

target_include_directories(${PROJECT_NAME} PUBLIC
	.
)

pico_generate_pio_header(${PROJECT_NAME} ${CMAKE_CURRENT_LIST_DIR}/lcd.pio)

target_link_libraries(${PROJECT_NAME}
	pico_stdlib
	pico_platform
	pico_multicore
	hardware_spi
	hardware_pio
	hardware_dma
	hardware_flash
	tinyusb_host
	tinyusb_board	
)

pico_enable_stdio_usb(${PROJECT_NAME} 0)
pico_enable_stdio_uart(${PROJECT_NAME} 0)

pico_add_extra_outputs(${PROJECT_NAME})
