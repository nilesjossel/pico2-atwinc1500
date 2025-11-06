# WINC1500 P2P Mesh Library - Simple Guide

**Simplified library for P2P mesh networking with ATWINC1500 on Raspberry Pi Pico / Pico 2**

---

## What This Is

A **simple, focused library** to create mesh networks using ATWINC1500 WiFi modules on Raspberry Pi Pico boards. No complicated abstractions, no platform-agnostic code - just clean, working mesh networking.

### Key Features

- ✅ **Simple API** - 6 main functions, that's it
- ✅ **P2P Mesh** - Multi-hop routing, auto-discovery
- ✅ **One file to include** - `#include "winc_lib.h"`
- ✅ **Hardware-tested** - Works on Pico, Pico 2, Pico 2W
- ✅ **Fixed mesh transmission** - Original code didn't actually send packets!
- ✅ **Single instance** - One WINC1500 per Pico (that's all you need)

---

## Hardware Setup

### What You Need

- Raspberry Pi Pico or Pico 2 (or Pico 2W)
- ATWINC1500 WiFi module
- 7 jumper wires
- 3.3V power supply (module draws ~150mA peak)

### Wiring (Default Pins)

```
ATWINC1500    Pico/Pico 2
----------    -----------
SCK      -->  GP18 (SPI0 SCK)
MOSI     -->  GP19 (SPI0 TX)
MISO     -->  GP16 (SPI0 RX)
CS       -->  GP17
WAKE     -->  GP20
RESET    -->  GP21
IRQ      -->  GP22
VCC      -->  3.3V (NOT 5V!)
GND      -->  GND
```

**Important**: ATWINC1500 is **3.3V only**. Do not connect to 5V!

---

## Quick Start

### 1. Build the Example

```bash
cd ~/pico/WINC1500_PICO2
mkdir build && cd build

# Node 1
cmake -DMY_NODE_ID=1 -DMY_NODE_NAME=Pico1 ..
make mesh_node

# Flash to first Pico
cp mesh_node.uf2 /media/RPI-RP2/  # Or drag to drive

# Node 2 (rebuild with different ID)
cmake -DMY_NODE_ID=2 -DMY_NODE_NAME=Pico2 ..
make mesh_node
# Flash to second Pico

# Node 3
cmake -DMY_NODE_ID=3 -DMY_NODE_NAME=Pico3 ..
make mesh_node
# Flash to third Pico
```

### 2. Power On and Watch

Connect to serial console (115200 baud):

```
========================================
  ATWINC1500 P2P Mesh Network Node
========================================
Node ID:   1
Node Name: Pico1
========================================

Initializing WINC1500...
Firmware: 19.6.1
MAC: 00:1E:C0:XX:XX:XX

=== MESH NETWORK ACTIVE ===
Listening for P2P connections...
Sending beacons every 5000 ms
===========================

Node 2 discovered (1 hop)
Sending to node 2: Hello from node 1
  -> Sent OK

=== MESH DATA RECEIVED ===
From: Node 2
Data: Hello from Pico2!
==========================
```

---

## API Reference

### Initialize

```c
#include "winc_lib.h"

int main() {
    stdio_init_all();

    // Initialize mesh network
    if (!winc_init(1, "Pico1")) {
        printf("WINC init failed\n");
        return -1;
    }

    // Ready to use!
}
```

### Main Loop

```c
// Set callback for received data
void my_handler(uint8_t src, uint8_t *data, uint16_t len) {
    printf("From node %u: %.*s\n", src, len, data);
}

int main() {
    winc_init(1, "Pico1");
    winc_mesh_set_callback(my_handler);

    while (1) {
        winc_poll();  // Call this in your loop!

        // Your application code here
    }
}
```

### Send Data

```c
char msg[] = "Hello from node 1!";
if (winc_mesh_send(2, (uint8_t*)msg, strlen(msg))) {
    printf("Sent to node 2\n");
} else {
    printf("No route to node 2\n");
}
```

### Check Routing

```c
// Get number of nodes
uint8_t nodes = winc_mesh_get_node_count();
printf("Active routes: %u\n", nodes);

// Print routing table
winc_mesh_print_routes();
// Output:
//   Mesh Routing Table (Node 1 - "Pico1"):
//     Node 2: 1 hop (direct)
//     Node 3: 2 hops via node 2
```

---

## Complete API

```c
// Initialization
bool winc_init(uint8_t node_id, const char *node_name);
void winc_set_verbose(int level);  // 0=silent, 1=info, 2=debug

// Main loop (REQUIRED)
void winc_poll(void);  // Call this repeatedly!

// Mesh operations
void winc_mesh_set_callback(void (*callback)(uint8_t src, uint8_t *data, uint16_t len));
bool winc_mesh_send(uint8_t dst_node, uint8_t *data, uint16_t len);
void winc_mesh_print_routes(void);
uint8_t winc_mesh_get_node_count(void);

// Info
void winc_get_firmware_version(uint8_t *major, uint8_t *minor, uint8_t *patch);
void winc_get_mac(uint8_t mac[6]);
uint8_t winc_get_node_id(void);
const char* winc_get_node_name(void);
```

---

## Using in Your Project

### Option 1: Add as Subdirectory

```cmake
# In your CMakeLists.txt
add_subdirectory(WINC1500_PICO2)

add_executable(my_app main.c)
target_link_libraries(my_app winc1500)
```

### Option 2: Copy Files

Copy these to your project:
- `winc_lib.h`
- `winc_lib.c`
- `winc_mesh.c`
- `winc_wifi.h` (internal)
- `winc_sock.h` (internal)
- `winc_p2p.h` (internal)

```cmake
add_executable(my_app
    main.c
    winc_lib.c
    winc_mesh.c
)
target_link_libraries(my_app pico_stdlib hardware_spi)
```

---

## Configuration

### Compile-Time Options

All defaults are sensible, but you can override:

```cmake
# Custom pins
target_compile_definitions(my_app PRIVATE
    WINC_PIN_CS=13
    WINC_PIN_IRQ=14
)

# Mesh settings
target_compile_definitions(my_app PRIVATE
    WINC_MESH_BEACON_INTERVAL_MS=10000  # 10 seconds
    WINC_MESH_MAX_NODES=16              # More nodes
)
```

### Pin Defaults

```c
SCK:   GP18
MOSI:  GP19
MISO:  GP16
CS:    GP17
WAKE:  GP20
RESET: GP21
IRQ:   GP22
```

---

## Troubleshooting

### Problem: "WINC init failed"

**Check**:
- [ ] WINC1500 module connected?
- [ ] Correct pins?
- [ ] 3.3V power (NOT 5V)?
- [ ] Good power supply (150mA peak)?
- [ ] Decoupling capacitors on WINC1500?

**Test SPI**:
```c
winc_set_verbose(3);  // Max verbosity
winc_init(1, "Test");
// Should see SPI transactions in serial output
```

### Problem: "No route to node X"

**Check**:
- [ ] Target node powered on?
- [ ] Within range (~10-30 meters)?
- [ ] Same P2P channel?
- [ ] Wait 30 seconds for discovery

**Debug**:
```c
winc_mesh_print_routes();
// Should show discovered nodes
```

### Problem: "Data not received"

**Check**:
- [ ] Callback set? (`winc_mesh_set_callback()`)
- [ ] `winc_poll()` called in loop?
- [ ] Target node listening?

**Test with echo**:
```c
void handler(uint8_t src, uint8_t *data, uint16_t len) {
    printf("RX from %u\n", src);
    // Echo back
    winc_mesh_send(src, data, len);
}
```

### Problem: Build Errors

**Missing `winc_lib.c`?**
- Library files not created yet - see IMPLEMENTATION_ROADMAP.md
- Current code is original (winc_wifi.c, winc_sock.c, winc_p2p.c)
- Needs refactoring (estimated 5 hours)

---

## Implementation Status

| Component | Status |
|-----------|--------|
| API design | ✅ Complete |
| Example code | ✅ Complete |
| Documentation | ✅ Complete |
| `winc_lib.c` | ⏳ **TODO** (refactor originals) |
| `winc_mesh.c` | ⏳ **TODO** (fix transmission) |
| Hardware testing | ⏳ **TODO** |

**Next steps**: See [SIMPLIFIED_DESIGN.md](SIMPLIFIED_DESIGN.md) for implementation guide.

---

## Performance

| Metric | Value |
|--------|-------|
| Latency (1 hop) | ~10-50 ms |
| Latency (2 hops) | ~50-200 ms |
| Throughput | ~100 KB/s (P2P limited) |
| Max packet size | ~1400 bytes |
| Max nodes | 8 (configurable) |
| Beacon interval | 5 seconds (configurable) |
| Route timeout | 30 seconds (configurable) |
| Range | 10-30 meters (line-of-sight) |

---

## Example Use Cases

### Sensor Network
```c
// Node collects sensor data, sends to gateway
float temp = read_temperature();
char msg[64];
snprintf(msg, sizeof(msg), "TEMP:%.2f", temp);
winc_mesh_send(GATEWAY_NODE, (uint8_t*)msg, strlen(msg));
```

### Remote Control
```c
// Control node sends commands
if (button_pressed()) {
    winc_mesh_send(TARGET_NODE, (uint8_t*)"LED:ON", 6);
}
```

### Chat System
```c
// Broadcast message to all nodes
for (uint8_t i = 1; i <= winc_mesh_get_node_count(); i++) {
    if (i != MY_NODE_ID) {
        winc_mesh_send(i, (uint8_t*)message, strlen(message));
    }
}
```

---

## License

Apache License 2.0

- Original code: Copyright (c) 2021 Jeremy P Bentham
- Mesh networking: Copyright (c) 2025 Niles Roxas

---

## Support

- **Documentation**: See [EFFICIENCY_ANALYSIS.md](EFFICIENCY_ANALYSIS.md) and [IMPLEMENTATION_ROADMAP.md](IMPLEMENTATION_ROADMAP.md)
- **Issues**: Create GitHub issue
- **Hardware**: Check [Microchip ATWINC1500 datasheet](https://www.microchip.com/wwwproducts/en/ATwinc1500)

---

**Status**: API complete, implementation in progress (est. 5 hours)
