# ATWINC1500 WiFi Driver Library for Raspberry Pi Pico / Pico 2

# Force Ninja generator
set(CMAKE_GENERATOR "Ninja" CACHE INTERNAL "" FORCE)

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
set(PICO_BOARD pico2 CACHE STRING "Board type")
SET(PICO_PLATFORM rp2350)

cmake_minimum_required(VERSION 3.13)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Pico SDK path (auto-detected or set manually)
#set(PICO_SDK_PATH "C:/Users/niles/pico/pico-sdk")

# Board selection: pico, pico2, or pico_w, pico2_w
set(PICO_BOARD pico2 CACHE STRING "Board type")

# Pull in Raspberry Pi Pico SDK
include(pico_sdk_import.cmake)

project(WINC1500_PICO2 C CXX ASM)

# Initialize Pico SDK
pico_sdk_init()

# ============================================================================
# WINC1500 WiFi Driver Library (static library for reuse in other projects)
# ============================================================================

add_library(winc1500 STATIC
    winc_lib.c
    winc_mesh.c
    # winc_wifi.c and winc_sock.c are now integrated into winc_lib.c
    # winc_p2p.c removed for now (P2P functionality in winc_lib.c)
)

target_include_directories(winc1500 PUBLIC
    ${CMAKE_CURRENT_LIST_DIR}
)

target_link_libraries(winc1500
    pico_stdlib
    pico_multicore
    hardware_spi
    hardware_irq
    hardware_interp
    hardware_clocks
    hardware_crc
    hardware_gpio
    hardware_adc
    hardware_watchdog
    hardware_dma
)

# Commented out - main.c does not exist
# add_executable(telemetry_system
#     main.c
#     winc_telemetry.c
#     winc_lib.c
#     winc_wifi.c
#     winc_sock.c
#     winc_mesh.c
# )

# ============================================================================
# Main Executable: WINC1500_PICO2
# ============================================================================

add_executable(WINC1500_PICO2 WINC1500_PICO2.c)

target_link_libraries(WINC1500_PICO2
    winc1500
)

# Node configuration for WINC1500_PICO2
if(DEFINED MY_NODE_ID)
    target_compile_definitions(WINC1500_PICO2 PRIVATE MY_NODE_ID=${MY_NODE_ID})
    message(STATUS "Building WINC1500_PICO2 with NODE_ID=${MY_NODE_ID}")
endif()

if(DEFINED MY_NODE_NAME)
    target_compile_definitions(WINC1500_PICO2 PRIVATE MY_NODE_NAME="${MY_NODE_NAME}")
    message(STATUS "Building WINC1500_PICO2 with NODE_NAME=${MY_NODE_NAME}")
endif()

pico_enable_stdio_uart(WINC1500_PICO2 1)
pico_enable_stdio_usb(WINC1500_PICO2 1)
pico_add_extra_outputs(WINC1500_PICO2)

# ============================================================================
# Example: Test program
# ============================================================================
# Commented out - test directory and files do not exist
# add_executable(test_tcp
#     test/test_tcp.c
#     winc_lib.c
#     winc_wifi.c
#     winc_sock.c
# )
#
# target_link_libraries(test_tcp
#     pico_stdlib
#     hardware_spi
#     hardware_gpio
#     hardware_crc
# )
#
# pico_enable_stdio_usb(test_tcp 1)
# pico_add_extra_outputs(test_tcp)
#
# add_executable(test_crc
#     test/test_crc.c
#     winc_telemetry.c
# )
#
# target_link_libraries(test_crc
#     pico_stdlib
#     hardware_crc
#     hardware_dma
# )
#
# pico_enable_stdio_usb(test_crc 1)
# pico_add_extra_outputs(test_crc)

# ============================================================================
# Example: Mesh Network Node
# ============================================================================

add_executable(mesh_node example_mesh_node.c)

target_link_libraries(mesh_node
    winc1500
)

pico_enable_stdio_uart(mesh_node 1)
pico_enable_stdio_usb(mesh_node 1)
pico_add_extra_outputs(mesh_node)

# ============================================================================
# Build Configuration Options
# ============================================================================
# Example: To build mesh_node with custom configuration:
#   cmake -DMY_NODE_ID=2 -DMY_NODE_NAME=Pico2 -DTARGET_NODE=1 ..
#   make mesh_node

# Node configuration
if(DEFINED MY_NODE_ID)
    target_compile_definitions(mesh_node PRIVATE MY_NODE_ID=${MY_NODE_ID})
    message(STATUS "Building mesh_node with NODE_ID=${MY_NODE_ID}")
endif()

if(DEFINED MY_NODE_NAME)
    target_compile_definitions(mesh_node PRIVATE MY_NODE_NAME="${MY_NODE_NAME}")
    message(STATUS "Building mesh_node with NODE_NAME=${MY_NODE_NAME}")
endif()

if(DEFINED TARGET_NODE)
    target_compile_definitions(mesh_node PRIVATE TARGET_NODE=${TARGET_NODE})
    message(STATUS "Building mesh_node with TARGET_NODE=${TARGET_NODE}")
endif()

# Custom pin configuration (optional)
# Example: cmake -DWINC_PIN_CS=13 -DWINC_PIN_IRQ=14 ..
if(DEFINED WINC_PIN_CS)
    target_compile_definitions(winc1500 PUBLIC WINC_PIN_CS=${WINC_PIN_CS})
endif()

if(DEFINED WINC_PIN_IRQ)
    target_compile_definitions(winc1500 PUBLIC WINC_PIN_IRQ=${WINC_PIN_IRQ})
endif()

if(DEFINED WINC_PIN_RESET)
    target_compile_definitions(winc1500 PUBLIC WINC_PIN_RESET=${WINC_PIN_RESET})
endif()

if(DEFINED WINC_PIN_WAKE)
    target_compile_definitions(winc1500 PUBLIC WINC_PIN_WAKE=${WINC_PIN_WAKE})
endif()

message(STATUS "========================================")
message(STATUS "ATWINC1500 WiFi Driver Library")
message(STATUS "Board type: ${PICO_BOARD}")
message(STATUS "Build type: ${CMAKE_BUILD_TYPE}")
message(STATUS "========================================")
