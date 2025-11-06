# ATWINC1500 P2P Mesh Library for Raspberry Pi Pico / Pico 2

cmake_minimum_required(VERSION 3.13)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Pico SDK path (auto-detected or set manually)
# set(PICO_SDK_PATH "C:/Users/niles/pico/pico-sdk")

# Board selection: pico, pico2, or pico_w, pico2_w
set(PICO_BOARD pico2 CACHE STRING "Board type")

# Pull in Raspberry Pi Pico SDK
include(pico_sdk_import.cmake)

project(WINC1500_MESH C CXX ASM)

# Initialize Pico SDK
pico_sdk_init()

# ============================================================================
# WINC1500 Library (static library for reuse in other projects)
# ============================================================================

add_library(winc1500 STATIC
    winc_lib.c
    winc_mesh.c
)

target_include_directories(winc1500 PUBLIC
    ${CMAKE_CURRENT_LIST_DIR}
)

target_link_libraries(winc1500
    pico_stdlib
    hardware_spi
)

# ============================================================================
# Example: Mesh Network Node
# ============================================================================

add_executable(mesh_node example_mesh_node.c)

target_link_libraries(
    mesh_node
    winc1500
)

pico_enable_stdio_uart(mesh_node 1)
pico_enable_stdio_usb(mesh_node 1)
pico_add_extra_outputs(mesh_node)

# ============================================================================
# Original example programs (for reference/testing)
# ============================================================================

# Original test from jbentham
add_executable(winc_wifi_original
    winc_pico_part2.c
    winc_wifi.c
    winc_sock.c
    winc_p2p.c
)

target_link_libraries(winc_wifi_original
    pico_stdlib
    hardware_spi
)

pico_enable_stdio_uart(winc_wifi_original 1)
pico_enable_stdio_usb(winc_wifi_original 0)
pico_add_extra_outputs(winc_wifi_original)

# Simple blink example
add_executable(simple_test WINC1500_PICO2.c)

target_link_libraries(simple_test
    pico_stdlib
    hardware_spi
)

pico_enable_stdio_uart(simple_test 1)
pico_enable_stdio_usb(simple_test 1)
pico_add_extra_outputs(simple_test)

# ============================================================================
# Build configuration options
# ============================================================================

# Example: To build mesh_node with custom configuration:
#   cmake -DMY_NODE_ID=2 -DMY_NODE_NAME=Pico2 -DTARGET_NODE=1 ..
#   make mesh_node

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
if(DEFINED WINC_PIN_CS)
    target_compile_definitions(winc1500 PUBLIC WINC_PIN_CS=${WINC_PIN_CS})
endif()

message(STATUS "Board type: ${PICO_BOARD}")
message(STATUS "Build type: ${CMAKE_BUILD_TYPE}")
