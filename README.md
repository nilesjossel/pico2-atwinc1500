# WINC1500 P2P Mesh Library for Raspberry Pi Pico 2

Simple, focused library for creating mesh networks using ATWINC1500 WiFi modules on Raspberry Pi Pico/Pico 2.

**Status**: 🚧 **Implementation In Progress** - See [CODE_REVIEW_FINAL.md](CODE_REVIEW_FINAL.md) for details

---

## Quick Start

### Hardware Requirements
- Raspberry Pi Pico or Pico 2 (non-2W model)
- ATWINC1500 WiFi module
- 3.3V power supply (150mA peak)

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
VCC      -->  3.3V  ⚠️ NOT 5V!
GND      -->  GND
```

### Build Example

```bash
cd ~/pico/WINC1500_PICO2
mkdir build && cd build

# Build for node 1
cmake -DMY_NODE_ID=1 -DMY_NODE_NAME=Pico1 ..
make mesh_node

# Flash to Pico
cp mesh_node.uf2 /media/RPI-RP2/
```

### Simple Usage

```c
#include "winc_lib.h"

void mesh_handler(uint8_t src, uint8_t *data, uint16_t len) {
    printf("From node %u: %.*s\n", src, len, data);
}

int main() {
    // Initialize
    winc_init(1, "Pico1");
    winc_mesh_set_callback(mesh_handler);

    while (1) {
        winc_poll();  // Handle events

        // Send to another node
        char msg[] = "Hello!";
        winc_mesh_send(2, (uint8_t*)msg, strlen(msg));

        sleep_ms(1000);
    }
}
```

---

## Project Status

### ✅ Complete
- Low-level SPI communication (winc_wifi.c)
- Socket layer (winc_sock.c)
- Routing table management
- P2P mode enable
- Example code

### ⚠️ Issues Found
1. **Mesh transmission incomplete** - Lines 221, 266, 304 in winc_p2p.c return true without sending
2. **Missing platform code** - winc_pico_part2.c not present
3. **Missing library impl** - winc_lib.c and winc_mesh.c need to be created

### ⏳ Implementation Needed
- [ ] Create `winc_pico_part2.c` (platform layer)
- [ ] Fix mesh transmission in `winc_p2p.c`
- [ ] Implement `winc_lib.c` and `winc_mesh.c`
- [ ] Test on hardware

**See [CODE_REVIEW_FINAL.md](CODE_REVIEW_FINAL.md) for complete analysis and fix instructions.**

---

## API Reference

### Core Functions
```c
// Initialize mesh network
bool winc_init(uint8_t node_id, const char *node_name);

// Poll for events (call in main loop)
void winc_poll(void);

// Set callback for received data
void winc_mesh_set_callback(void (*callback)(uint8_t src, uint8_t *data, uint16_t len));

// Send data to another node
bool winc_mesh_send(uint8_t dst_node, uint8_t *data, uint16_t len);

// Print routing table (debug)
void winc_mesh_print_routes(void);
```

### Configuration
Pin assignments can be overridden at compile time:
```bash
cmake -DWINC_PIN_CS=13 -DWINC_PIN_IRQ=14 ..
```

Default pins defined in [winc_lib.h](winc_lib.h) lines 13-46.

---

## File Organization

```
WINC1500_PICO2/
├── README.md                          # This file
├── CODE_REVIEW_FINAL.md               # Complete code analysis
├── SIMPLIFIED_DESIGN.md               # Design decisions
├── IMPLEMENTATION_NEXT_STEPS.md       # Implementation guide
│
├── winc_lib.h                         # Public API
├── winc_wifi.c/h                      # SPI/HIF layer
├── winc_sock.c/h                      # Socket layer
├── winc_p2p.c/h                       # P2P/Mesh layer
├── example_mesh_node.c                # Example application
│
├── winc_lib.c                         # ⏳ TO CREATE
├── winc_mesh.c                        # ⏳ TO CREATE
├── winc_pico_part2.c                  # ⏳ TO CREATE
│
├── CMakeLists.txt                     # Build configuration
└── docs/
    └── archive/                       # Historical documents
        ├── EFFICIENCY_ANALYSIS.md
        └── IMPLEMENTATION_ROADMAP.md
```

---

## Documentation

- **[CODE_REVIEW_FINAL.md](CODE_REVIEW_FINAL.md)** - Comprehensive code analysis (READ THIS FIRST)
- **[SIMPLIFIED_DESIGN.md](SIMPLIFIED_DESIGN.md)** - Architecture and design decisions
- **[IMPLEMENTATION_NEXT_STEPS.md](IMPLEMENTATION_NEXT_STEPS.md)** - Step-by-step implementation guide
- **[README_SIMPLE.md](README_SIMPLE.md)** - Detailed usage guide

---

## Known Issues

### Critical
1. **Mesh transmission not implemented** (winc_p2p.c:221, 266, 304)
   - Functions build packets but don't call `put_sock_sendto()`
   - Returns `true` without actually sending
   - **Fix**: Add UDP socket integration

2. **Missing platform functions** (winc_pico_part2.c)
   - `usec()`, `spi_xfer()`, `read_irq()` undefined
   - **Fix**: Create platform integration file

3. **Library not implemented** (winc_lib.c, winc_mesh.c)
   - Headers exist but no implementation
   - **Fix**: Create implementation files (templates in IMPLEMENTATION_NEXT_STEPS.md)

### Build Errors
```
❌ mesh_node:          undefined reference to `winc_init'
❌ winc_wifi_original: winc_pico_part2.c: No such file
✅ simple_test:        Builds OK (no WINC dependencies)
```

---

## Troubleshooting

### Build Fails
**Problem**: "undefined reference to winc_init"
**Solution**: Library files not created yet. See [IMPLEMENTATION_NEXT_STEPS.md](IMPLEMENTATION_NEXT_STEPS.md)

### No Mesh Discovery
**Problem**: Nodes don't find each other
**Cause**: Mesh transmission incomplete (line 221 in winc_p2p.c)
**Solution**: Fix requires adding `put_sock_sendto()` call

### Can't Flash to Pico
**Problem**: No .uf2 file generated
**Cause**: Build failed due to missing files
**Solution**: Check [CODE_REVIEW_FINAL.md](CODE_REVIEW_FINAL.md) Section "CLEANUP PLAN"

---

## Contributing

This is a work-in-progress library. Current priorities:

1. Complete missing implementation files
2. Fix mesh transmission bugs
3. Test on hardware with multiple nodes
4. Add error handling

See [CODE_REVIEW_FINAL.md](CODE_REVIEW_FINAL.md) for detailed implementation plan.

---

## License

Apache License 2.0

- Original code: Copyright (c) 2021 Jeremy P Bentham ([jbentham/winc_wifi](https://github.com/jbentham/winc_wifi))
- Mesh networking: Copyright (c) 2025 Niles Roxas

---

## Support

- **Documentation**: See above links
- **Hardware**: [ATWINC1500 Datasheet](https://www.microchip.com/wwwproducts/en/ATwinc1500)

---

**Last Updated**: October 29, 2025

