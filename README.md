# ATWINC1500 WiFi Driver Library for Raspberry Pi Pico 2

A simple and focused WiFi driver library for the ATWINC1500 WiFi module on Raspberry Pi Pico and Pico 2. This library provides low-level SPI communication, initialization routines, and example code for WiFi connectivity and P2P mesh networking.

## Features

- **SPI Interface**: Hardware SPI communication with the ATWINC1500 module
- **Initialization Routines**: Simple API to initialize and configure the WiFi module
- **WiFi Connectivity**: Support for WiFi connections and network operations
- **P2P Mesh Networking**: Built-in support for creating mesh networks between multiple devices
- **Socket Layer**: UDP and TCP socket support for network communication
- **Customizable Pin Configuration**: Override default GPIO pins at compile time
- **Example Code**: Ready-to-use examples for getting started quickly

## Hardware Requirements

- Raspberry Pi Pico or Pico 2 (non-W models)
- ATWINC1500 WiFi module
- 3.3V power supply (150mA peak current)

### Wiring (Default Pins)

```
ATWINC1500    Pico 2
----------    ------
SCK      -->  GP18
MOSI     -->  GP19
MISO     -->  GP16
CS       -->  GP17
WAKE     -->  GP20
RESET    -->  GP21
IRQ      -->  GP22
VCC      -->  3.3V  (WARNING: NOT 5V!)
GND      -->  GND
```

## Installation

### 1. Clone the Repository

```bash
cd ~/pico
cd WINC1500_PICO2
```

### 2. Set Up Pico SDK

Make sure you have the Raspberry Pi Pico SDK installed. If not:

```bash
cd ~/pico
git clone https://github.com/raspberrypi/pico-sdk.git
cd pico-sdk
git submodule update --init
export PICO_SDK_PATH=~/pico/pico-sdk
```

### 3. Build the Project

```bash
cd ~/pico/WINC1500_PICO2
mkdir build && cd build
cmake ..
make
```

### 4. Flash to Your Pico

```bash
# Hold BOOTSEL button, plug in Pico, then release
cp mesh_node.uf2 /media/RPI-RP2/
```

## Usage Example

Here's a simple example showing how to initialize the ATWINC1500 and create a mesh network:

```c
#include <stdio.h>
#include <string.h>
#include "pico/stdlib.h"
#include "winc_lib.h"

// Callback function for received data
void mesh_data_received(uint8_t src_node, uint8_t *data, uint16_t len) {
    printf("Received from node %u: %.*s\n", src_node, len, data);
}

int main() {
    stdio_init_all();

    // Initialize ATWINC1500 with node ID and name
    if (!winc_init(1, "Pico1")) {
        printf("WINC initialization failed!\n");
        return -1;
    }

    // Set callback for received data
    winc_mesh_set_callback(mesh_data_received);

    printf("ATWINC1500 initialized successfully\n");

    while (1) {
        // Poll for events (call this regularly in main loop)
        winc_poll();

        // Send a message to node 2
        char message[] = "Hello from Pico 1!";
        if (winc_mesh_send(2, (uint8_t*)message, strlen(message))) {
            printf("Message sent to node 2\n");
        }

        sleep_ms(5000);  // Send every 5 seconds
    }

    return 0;
}
```

## API Reference

### Core Functions

```c
// Initialize WINC1500 and start mesh network
bool winc_init(uint8_t node_id, const char *node_name);

// Poll for events - call this in your main loop
void winc_poll(void);

// Set callback for received mesh data
void winc_mesh_set_callback(void (*callback)(uint8_t src, uint8_t *data, uint16_t len));

// Send data to another mesh node
bool winc_mesh_send(uint8_t dst_node, uint8_t *data, uint16_t len);

// Print routing table (for debugging)
void winc_mesh_print_routes(void);

// Get number of active routes
uint8_t winc_mesh_get_node_count(void);

// Get firmware version
void winc_get_firmware_version(uint8_t *major, uint8_t *minor, uint8_t *patch);

// Get MAC address
void winc_get_mac(uint8_t mac[6]);
```

### Configuration

You can override default pin assignments at compile time:

```bash
cmake -DWINC_PIN_CS=13 -DWINC_PIN_IRQ=14 ..
make
```

For mesh node configuration:

```bash
cmake -DMY_NODE_ID=2 -DMY_NODE_NAME=Pico2 ..
make mesh_node
```

## Building for Multiple Nodes

To create a mesh network, build and flash different node IDs to each Pico:

```bash
# Build for Node 1
cmake -DMY_NODE_ID=1 -DMY_NODE_NAME=Pico1 ..
make mesh_node
cp mesh_node.uf2 /media/RPI-RP2/

# Build for Node 2
cmake -DMY_NODE_ID=2 -DMY_NODE_NAME=Pico2 ..
make mesh_node
cp mesh_node.uf2 /media/RPI-RP2/
```

## Project Structure

```
WINC1500_PICO2/
├── README.md                    # This file
├── CMakeLists.txt              # Build configuration
├── pico_sdk_import.cmake       # Pico SDK import
├── winc_lib.h                  # Public API header
├── winc_lib.c                  # Library implementation
├── winc_mesh.c                 # Mesh networking layer
├── winc_wifi.c/h               # Low-level WiFi/SPI driver
├── winc_sock.c/h               # Socket layer
├── winc_p2p.c/h                # P2P networking
└── example_mesh_node.c         # Example mesh node application
```

## License

MIT License

Copyright (c) 2025 Niles Roxas
Original WINC driver code Copyright (c) 2021 Jeremy P Bentham

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.

## Acknowledgments

- Original WINC1500 driver by [Jeremy P Bentham](https://github.com/jbentham/winc_wifi)
- Built for the Raspberry Pi Pico SDK

## Support

For issues, questions, or contributions, please visit the project repository or consult the [ATWINC1500 datasheet](https://www.microchip.com/wwwproducts/en/ATwinc1500).
