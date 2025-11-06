# WINC1500 Mesh Library - Simplified Design

**Purpose**: Simple, focused library for P2P mesh networking with ATWINC1500 on Raspberry Pi Pico/Pico 2.

---

## Design Philosophy

✅ **KEEP IT SIMPLE**
- Single instance only (one WINC1500 per Pico)
- Pico/Pico 2 only (no generic BSP layer)
- Focused on P2P mesh networking
- Direct hardware access (no abstraction overkill)

❌ **NO UNNECESSARY COMPLEXITY**
- No multi-platform support
- No BSP abstraction layer
- No complicated state machines
- No over-engineered APIs

---

## What You Actually Need

### Core Library Files (3 files)

```
winc_lib.h          ← Single public header
winc_lib.c          ← Core implementation (SPI + HIF + WiFi + Sockets)
winc_mesh.c/h       ← Mesh networking layer
```

### Use in Your Project

```c
#include "winc_lib.h"

int main() {
    // Initialize with node ID
    winc_init(1, "Node1");

    // Set mesh data callback
    winc_mesh_set_callback(my_handler);

    // Main loop
    while (1) {
        winc_poll();  // Handle everything

        // Send to another node
        winc_mesh_send(2, data, len);
    }
}
```

That's it!

---

## Context Structure (Simple)

```c
typedef struct {
    // Hardware pins (set once at init)
    struct {
        uint8_t sck, mosi, miso, cs;
        uint8_t wake, reset, irq;
    } pins;

    // SPI buffers
    uint8_t txbuf[1600];
    uint8_t rxbuf[1600];

    // Socket state
    struct {
        uint16_t port;
        uint8_t state;
        void (*handler)(uint8_t *data, uint16_t len);
    } sockets[10];

    // Mesh state
    struct {
        uint8_t my_node_id;
        char my_name[16];
        uint8_t p2p_channel;
        bool enabled;

        // Routing table
        struct {
            uint8_t node_id;
            uint8_t next_hop;
            uint8_t hop_count;
            uint32_t last_seen;
        } routes[8];
        uint8_t route_count;

        // Mesh socket
        int udp_sock;

        // Callback
        void (*data_callback)(uint8_t src, uint8_t *data, uint16_t len);
    } mesh;

    // Config
    int verbose;
} winc_context_t;
```

**Key point**: Single global context (since only one WINC1500 per board):

```c
static winc_context_t g_ctx;  // Global is OK for single instance
```

---

## API Design (Ultra Simple)

### Initialization
```c
// Initialize library with mesh node ID and name
void winc_init(uint8_t node_id, const char *name);

// Set verbose level (0=silent, 1=info, 2=debug)
void winc_set_verbose(int level);
```

### Main Loop
```c
// Poll for events (call in main loop)
void winc_poll(void);
```

### Mesh Operations
```c
// Set callback for received mesh data
void winc_mesh_set_callback(void (*callback)(uint8_t src_node, uint8_t *data, uint16_t len));

// Send data to a node
bool winc_mesh_send(uint8_t dst_node, uint8_t *data, uint16_t len);

// Print routing table (debug)
void winc_mesh_print_routes(void);
```

### Low-Level (Optional)
```c
// Get firmware version
void winc_get_firmware_version(uint8_t *major, uint8_t *minor, uint8_t *patch);

// Get MAC address
void winc_get_mac(uint8_t mac[6]);
```

---

## File Structure (Minimal)

```
WINC1500_PICO2/
├── winc_lib.h              ← Public API (include this)
├── winc_lib.c              ← Core implementation
├── winc_mesh.h             ← Mesh API (included by winc_lib.h)
├── winc_mesh.c             ← Mesh implementation
├── example_mesh_node.c     ← Simple example
├── CMakeLists.txt          ← Build config
└── README_SIMPLE.md        ← Usage guide
```

---

## Implementation Strategy

### Phase 1: Refactor with Context (2 hours)

**Current files**:
- `winc_wifi.c` (SPI + HIF + WiFi)
- `winc_sock.c` (Sockets)
- `winc_p2p.c` (P2P + Mesh)

**Refactor to**:
- `winc_lib.c` (merge all three, add context parameter)
- `winc_mesh.c` (separate out mesh logic)

**Pattern**:
```c
// BEFORE
int spi_read_reg(int fd, uint32_t addr, uint32_t *valp) {
    CMD_MSG_A *mp = (CMD_MSG_A *)txbuff;  // Global!
    // ...
}

// AFTER
static int spi_read_reg(uint32_t addr, uint32_t *valp) {
    winc_context_t *ctx = &g_ctx;  // Single global context
    CMD_MSG_A *mp = (CMD_MSG_A *)ctx->txbuf;
    // ...
}
```

### Phase 2: Fix Mesh Transmission (1 hour)

**CRITICAL FIX** - Actually send packets:

```c
bool winc_mesh_send_beacon(void) {
    winc_context_t *ctx = &g_ctx;

    // Build beacon
    mesh_beacon_t beacon;
    beacon.node_id = ctx->mesh.my_node_id;
    strncpy(beacon.name, ctx->mesh.my_name, sizeof(beacon.name));

    // ✅ ACTUALLY SEND IT
    return sock_sendto(ctx->mesh.udp_sock, &beacon, sizeof(beacon));
}

bool winc_mesh_send(uint8_t dst_node, uint8_t *data, uint16_t len) {
    winc_context_t *ctx = &g_ctx;

    // Find route
    int next_hop = find_route(dst_node);
    if (next_hop < 0) return false;

    // Build packet
    mesh_packet_t pkt;
    pkt.src = ctx->mesh.my_node_id;
    pkt.dst = dst_node;
    pkt.len = len;
    memcpy(pkt.data, data, len);

    // ✅ ACTUALLY SEND IT
    return sock_sendto(ctx->mesh.udp_sock, &pkt, sizeof(pkt));
}
```

### Phase 3: Create Simple Example (30 min)

```c
#include "winc_lib.h"

// Node ID set at compile time (change for each board)
#define MY_NODE_ID  1
#define MY_NAME     "Pico1"

void mesh_data_received(uint8_t src, uint8_t *data, uint16_t len) {
    printf("From node %u: %.*s\n", src, len, data);
}

int main() {
    stdio_init_all();

    // Initialize mesh network
    winc_init(MY_NODE_ID, MY_NAME);
    winc_mesh_set_callback(mesh_data_received);

    printf("Mesh node %u (%s) started\n", MY_NODE_ID, MY_NAME);

    uint32_t last_send = 0;
    while (1) {
        // Handle events
        winc_poll();

        // Send test message every 10 seconds
        uint32_t now = to_ms_since_boot(get_absolute_time());
        if (now - last_send > 10000) {
            char msg[64];
            snprintf(msg, sizeof(msg), "Hello from node %u", MY_NODE_ID);

            // Send to node 2 (change as needed)
            winc_mesh_send(2, (uint8_t*)msg, strlen(msg));
            last_send = now;
        }
    }
}
```

---

## CMakeLists.txt (Simple)

```cmake
cmake_minimum_required(VERSION 3.13)

include(pico_sdk_import.cmake)
project(winc1500_mesh C CXX ASM)
set(PICO_BOARD pico2)
pico_sdk_init()

# ===== Library =====
add_library(winc1500 STATIC
    winc_lib.c
    winc_mesh.c
)

target_link_libraries(winc1500
    pico_stdlib
    hardware_spi
)

# ===== Example =====
add_executable(mesh_node example_mesh_node.c)
target_link_libraries(mesh_node winc1500)
pico_enable_stdio_uart(mesh_node 1)
pico_enable_stdio_usb(mesh_node 1)
pico_add_extra_outputs(mesh_node)
```

---

## Pin Configuration (Hardcoded)

Since you're only targeting Pico/Pico 2, hardcode sensible defaults:

```c
// In winc_lib.c
#define WINC_PIN_SCK    18
#define WINC_PIN_MOSI   19
#define WINC_PIN_MISO   16
#define WINC_PIN_CS     17
#define WINC_PIN_WAKE   20
#define WINC_PIN_RESET  21
#define WINC_PIN_IRQ    22

#define WINC_SPI_PORT   spi0
#define WINC_SPI_SPEED  11000000  // 11 MHz
```

Can override at compile time if needed:
```cmake
target_compile_definitions(mesh_node PRIVATE
    WINC_PIN_CS=13
    # ... other pins
)
```

---

## Flashing Multiple Nodes

**Option 1**: Compile-time node ID
```bash
# Node 1
cmake -DNODE_ID=1 -DNODE_NAME=Pico1 ..
make
cp mesh_node.uf2 /media/RPI-RP2/

# Node 2
cmake -DNODE_ID=2 -DNODE_NAME=Pico2 ..
make
cp mesh_node.uf2 /media/RPI-RP2/
```

**Option 2**: Read from EEPROM/Flash
```c
// Store node config in flash
typedef struct {
    uint32_t magic;
    uint8_t node_id;
    char name[16];
} node_config_t;

// Read at startup
void winc_init_from_flash(void) {
    node_config_t *cfg = (node_config_t*)0x10100000;  // Flash address
    if (cfg->magic == 0xDEADBEEF) {
        winc_init(cfg->node_id, cfg->name);
    }
}
```

**Option 3**: USB serial config (simplest for testing)
```c
printf("Enter node ID: ");
int node_id = getchar() - '0';

printf("Enter name: ");
char name[16];
scanf("%15s", name);

winc_init(node_id, name);
```

---

## Testing Multiple Nodes

1. **Flash 3 boards** with different node IDs (1, 2, 3)
2. **Power them up** within range
3. **Watch serial output**:
   ```
   Node 1: Discovered node 2 (1 hop)
   Node 1: Discovered node 3 (1 hop)
   Node 1: Routing table: 2 nodes
   Node 1: Sending to node 2
   Node 1: Received from node 2: "Hello from Pico2"
   ```

4. **Test routing** - move node 3 out of range of node 1
   ```
   Node 1: Node 3 via node 2 (2 hops)
   Node 1: Sending to node 3 (routed via node 2)
   ```

---

## What Gets Removed

❌ **Remove from current design**:
- `lib/include/winc_bsp.h` - No BSP abstraction
- `lib/include/winc_m2m.h` - No SDK-style API
- `winc_context.h` with function pointers - Just use struct
- Multiple error enums - Use `bool` or `int`
- Callback registration - Single callback per feature
- Platform abstraction - Direct Pico SDK calls

✅ **Keep**:
- Context structure (but simpler)
- Mesh routing logic
- P2P transmission fix
- Example code

---

## Timeline (Simplified)

| Task | Time |
|------|------|
| Merge files into winc_lib.c | 1 hour |
| Add context parameter to all functions | 2 hours |
| Fix mesh transmission | 1 hour |
| Create example | 30 min |
| Test on hardware | 1 hour |
| **TOTAL** | **5.5 hours** |

**Down from 25 hours!**

---

## Key Decisions

1. **Single global context**: Since one WINC1500 per board, global is fine
2. **Hardcoded pins**: Sensible defaults, override via defines if needed
3. **Merged files**: Easier to maintain, less indirection
4. **Focused on mesh**: Remove WiFi client stuff (can add later if needed)
5. **Simple callbacks**: One callback for mesh data, that's it

---

## Next Steps

1. Create `winc_lib.h` with simple API
2. Merge `winc_wifi.c`, `winc_sock.c`, `winc_p2p.c` → `winc_lib.c`
3. Add `g_ctx` global and refactor functions to use it
4. Extract mesh logic to `winc_mesh.c`
5. Fix `mesh_send_beacon()` and `mesh_send_data()`
6. Create `example_mesh_node.c`
7. Test!

---

**This is what you actually need - simple, focused, works.**
