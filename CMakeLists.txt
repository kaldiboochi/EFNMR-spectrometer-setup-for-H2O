# Set the minimum version of CMake required for this project.
cmake_minimum_required(VERSION 3.13)

# Pull in the Raspberry Pi Pico SDK.
include($ENV{PICO_SDK_PATH}/pico_sdk_init.cmake)

# Define the project name.
project(pio_dac_sine C CXX ASM)

# Initialize the Pico SDK for our project.
pico_sdk_init()

# --- Main Project Configuration ---

# 1. DEFINE THE EXECUTABLE (MUST BE FIRST)
#    Create the executable target from your C source files.
add_executable(pio_dac_sine
    main.c
)

# --- NOW MODIFY THE "pio_dac_sine" TARGET ---

# 2. ADD PIO SOURCES
#    Add the auto-generated PIO header to the target.
pico_generate_pio_header(pio_dac_sine "${CMAKE_CURRENT_SOURCE_DIR}/dac_trigger.pio")

# 3. CONFIGURE STDIO
#    Enable stdio (printf) over USB and disable it for UART.
pico_enable_stdio_usb(pio_dac_sine 1)
pico_enable_stdio_uart(pio_dac_sine 0)

# 4. LINK LIBRARIES
#    Link all the necessary SDK libraries to your executable.
target_link_libraries(pio_dac_sine PRIVATE
    pico_stdlib
    hardware_pio
    hardware_clocks
    hardware_irq
    hardware_adc
)

# 5. CREATE OUTPUT FILES
#    Generate the .uf2 file for drag-and-drop programming.
pico_add_extra_outputs(pio_dac_sine)
