# ATWINC1500 Mesh Networking for Raspberry Pi Pico 2W

This project extends the ATWINC1500/1510 WiFi driver to support mesh networking on the Raspberry Pi Pico 2W (RP2350) using P2P (Wi-Fi Direct) mode.

## Overview

The ATWINC1500 WiFi module supports P2P (Peer-to-Peer) mode, which allows devices to connect directly to each other without requiring a traditional access point. This implementation adds a mesh networking layer on top of P2P to enable multi-hop communication between multiple Pico 2W devices.

## Hardware Requirements

- Raspberry Pi Pico 2W (RP2350-based board)
- ATWINC1500 or ATWINC1510 WiFi module
- Connections:
  - SPI0 interface
  - SCK: GPIO 18
  - MOSI: GPIO 19
  - MISO: GPIO 16
  - CS: GPIO 17
  - WAKE: GPIO 20
  - RESET: GPIO 21
  - IRQ: GPIO 22

## Software Requirements

- Pico SDK (configured for Pico 2W support)
- CMake 3.13 or later
- ARM GCC toolchain

## Features

### P2P Mode
- Enable/disable P2P mode on ATWINC1500
- Device discovery and peer detection
- WPS-PBC (Push Button Configuration) connection
- WPS-PIN connection support
- P2P listen and search modes

### Mesh Networking
- Automatic neighbor discovery via beacons
- Multi-hop routing (up to 4 hops by default)
- Routing table management
- Automatic route timeout and cleanup
- Support for up to 8 nodes in the mesh
- TCP and UDP socket support for mesh data

## File Structure

```
part2/
├── winc_pico_part2.c    - Main application with mesh mode support
├── winc_wifi.c          - Core ATWINC1500 driver
├── winc_wifi.h          - Driver header
├── winc_sock.c          - Socket implementation
├── winc_sock.h          - Socket header
├── winc_p2p.c           - NEW: P2P and mesh implementation
├── winc_p2p.h           - NEW: P2P and mesh definitions
├── mesh_example.c       - NEW: Standalone mesh example
├── CMakeLists.txt       - Updated for Pico 2W and mesh support
└── README_MESH.md       - This file
```

## Building the Project

### Prerequisites

1. Set up the Pico SDK for your system
2. Update the `PICO_SDK_PATH` in `CMakeLists.txt` to point to your SDK installation

### Build Steps

```bash
cd part2
mkdir build
cd build
cmake ..
make
```

This will generate:
- `winc_wifi.uf2` - Main application with mesh support

### Configuration Options

In `winc_pico_part2.c`, you can configure:

```c
#define ENABLE_MESH_MODE 1      // 1 = mesh mode, 0 = standard WiFi
#define MESH_NODE_ID     1      // Unique ID (1-255) for this node
#define MESH_NODE_NAME   "PicoNode1"  // Descriptive name
```

**Important:** Each device in your mesh network must have a unique `MESH_NODE_ID`.

## Usage

### Standard Mode (ENABLE_MESH_MODE = 0)

The device operates as a normal WiFi client, connecting to an access point and providing TCP/UDP echo services.

### Mesh Mode (ENABLE_MESH_MODE = 1)

1. **Flash multiple Pico 2W devices** with different NODE_IDs:
   - Device 1: `MESH_NODE_ID = 1`, `MESH_NODE_NAME = "PicoNode1"`
   - Device 2: `MESH_NODE_ID = 2`, `MESH_NODE_NAME = "PicoNode2"`
   - Device 3: `MESH_NODE_ID = 3`, `MESH_NODE_NAME = "PicoNode3"`
   - etc.

2. **Power on the devices** - they will automatically:
   - Enable P2P mode on channel 1
   - Start listening for peer connections
   - Send periodic beacons every 5 seconds
   - Build routing tables based on discovered neighbors

3. **Monitor via serial console** (115200 baud):
   ```
   === Mesh Networking Mode ===
   Node ID: 1
   Node Name: PicoNode1
   ===========================

   Enabling P2P mode on channel 1
   P2P mode enabled
   Mesh networking enabled

   Waiting for P2P connections...
   ```

4. **View routing table** - Automatically prints every 30 seconds:
   ```
   === Mesh Routing Table ===
   Local Node ID: 1 (PicoNode1)
   Active Nodes: 2
   Node ID  Hops  Next Hop  Active
   -------  ----  --------  ------
       2     1       2       Yes
       3     2       2       Yes
   ========================
   ```

### Mesh Example Application

A standalone example is provided in `mesh_example.c` that demonstrates:
- Complete mesh setup
- Custom data handlers
- Status monitoring
- Routing table visualization

## API Reference

### P2P Functions

```c
// Enable P2P mode
bool p2p_enable(int fd, uint8_t channel);

// Disable P2P mode
bool p2p_disable(int fd);

// Start P2P listen mode
bool p2p_start_listen(int fd, uint8_t channel);

// Start P2P device search
bool p2p_start_search(int fd);

// Connect using WPS Push Button
bool p2p_connect_wps_pbc(int fd);

// Connect using WPS PIN
bool p2p_connect_wps_pin(int fd, uint8_t *pin);
```

### Mesh Functions

```c
// Initialize mesh network
bool mesh_init(int fd, uint8_t node_id, char *node_name);

// Enable mesh networking
bool mesh_enable(int fd);

// Disable mesh networking
bool mesh_disable(int fd);

// Send beacon packet
bool mesh_send_beacon(int fd);

// Send data to destination node
bool mesh_send_data(int fd, uint8_t dst_node, uint8_t *data, uint16_t len);

// Print routing table
void mesh_print_routing_table(void);

// Check if P2P is enabled
bool is_p2p_enabled(void);

// Check if mesh is enabled
bool is_mesh_enabled(void);
```

## Configuration Parameters

### In winc_p2p.h:

```c
#define MESH_MAX_NODES      8      // Maximum nodes in mesh
#define MESH_BEACON_INTERVAL 5000  // Beacon interval (ms)
#define MESH_ROUTE_TIMEOUT  30000  // Route timeout (ms)
#define MESH_MAX_HOPS       4      // Maximum hops
```

### P2P Channels:

```c
#define P2P_CHAN_1          1
#define P2P_CHAN_6          6
#define P2P_CHAN_11         11
```

## Mesh Network Topology

The mesh network automatically adapts to available connections:

```
     Node1 -------- Node2
       |              |
       |              |
     Node3 -------- Node4
```

- Each node maintains a routing table
- Beacons are sent periodically to announce presence
- Routes are updated based on hop count
- Stale routes are removed after timeout

## Limitations and Notes

1. **ATWINC1500 P2P Limitations:**
   - The ATWINC1500 P2P implementation is simplified
   - Only supports basic P2P connections
   - No advanced P2P group formation features

2. **Mesh Implementation:**
   - This is a **simple mesh implementation** for demonstration
   - Not a full-featured mesh protocol (e.g., not IEEE 802.11s)
   - Route selection is based on hop count only
   - No advanced features like route optimization, load balancing, etc.

3. **Performance:**
   - Mesh adds overhead for beacons and routing
   - Actual data throughput depends on hop count
   - More hops = more latency

4. **Compatibility:**
   - Designed for Pico 2W (RP2350) but should work on original Pico with minor changes
   - Update `PICO_BOARD` in CMakeLists.txt if using original Pico

## Troubleshooting

### P2P mode fails to enable
- Check ATWINC1500 firmware version (needs P2P support)
- Verify SPI connections
- Check power supply (ATWINC1500 can draw significant current)

### No peer connections
- Ensure all devices are on the same P2P channel
- Check that devices have unique node IDs
- Verify IRQ line is working (watch for interrupt messages)

### Routing table empty
- Wait for beacon interval (5 seconds)
- Check verbose output for beacon messages
- Verify sockets are properly initialized

### Build errors
- Ensure PICO_SDK_PATH is correct
- Update to latest Pico SDK for Pico 2W support
- Check that all source files are included in CMakeLists.txt

## References

1. **iosoft.blog ATWINC1500 articles:**
   - Part 1: https://iosoft.blog/2021/05/24/winc_wifi/
   - Part 2: https://iosoft.blog/2021/06/01/winc_wifi_part2/

2. **ATWINC1500 P2P Mode Application Note:**
   - Microchip Document: AT14614

3. **ATWINC15x0 Datasheet:**
   - Microchip Document: DS70005305D

4. **Raspberry Pi Pico 2W Documentation:**
   - https://www.raspberrypi.com/documentation/microcontrollers/

5. **WINC1500 Documentation:**
   - https://documentation.help/WINC1500/documentation.pdf
   - https://ww1.microchip.com/downloads/en/DeviceDoc/Atmel-42420-WINC1500-Software-Design-Guide_UserGuide.pdf

## Future Enhancements

Potential improvements for the mesh implementation:

1. **Route Quality Metrics:**
   - Add RSSI-based route selection
   - Link quality estimation
   - Preferred path selection

2. **Mesh Protocol Features:**
   - Route discovery protocol (RREQ/RREP)
   - Route maintenance messages
   - Sequence number-based loop prevention

3. **Application Layer:**
   - Chat application example
   - Sensor data collection
   - Distributed control

4. **Power Management:**
   - Sleep mode support
   - Duty-cycled beacons
   - Wake-on-demand

## License

Based on original work by Jeremy P Bentham, Copyright (c) 2021
Modified for Pico 2W and mesh networking support, 2025

Licensed under the Apache License, Version 2.0

## Contributing

This is a basic implementation meant for educational purposes and simple projects. Contributions to improve reliability, performance, and features are welcome!

---

For questions or issues, please refer to the original blog posts and ATWINC1500 documentation.
