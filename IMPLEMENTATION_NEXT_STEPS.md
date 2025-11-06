# WINC1500 Library - Implementation Next Steps

**Context**: You asked to simplify the over-engineered design and focus on **P2P mesh networking only** for **Pico/Pico 2 with one WINC1500 module per board**.

---

## ✅ What's Done

1. **Simplified Design** - Removed BSP layer, multi-platform support, complex abstractions
2. **Clean API** - `winc_lib.h` with 6 main functions
3. **Example Code** - `example_mesh_node.c` ready to use
4. **CMakeLists.txt** - Library build configured
5. **Documentation** - README_SIMPLE.md and SIMPLIFIED_DESIGN.md

---

## ⏳ What Needs to Be Done

### Files to Create

You need to merge the original files into simplified versions:

```
winc_lib.c       ← Merge winc_wifi.c + winc_sock.c + refactor with context
winc_mesh.c      ← Extract from winc_p2p.c + FIX packet transmission
```

---

## 🔧 Implementation Steps (5 Hours Total)

### Step 1: Create `winc_lib.c` (3 hours)

**Goal**: Merge `winc_wifi.c` and `winc_sock.c` into single file with context

**File structure**:
```c
// winc_lib.c

#include "winc_lib.h"
#include "winc_wifi.h"  // Keep original definitions
#include "winc_sock.h"
#include "pico/stdlib.h"
#include "hardware/spi.h"

// ===== SINGLE GLOBAL CONTEXT =====
typedef struct {
    // Hardware
    struct {
        uint8_t sck, mosi, miso, cs, wake, reset, irq;
    } pins;

    // SPI buffers (from winc_wifi.c globals)
    uint8_t txbuf[1600];
    uint8_t rxbuf[1600];
    uint8_t tx_zeros[1024];

    // Sockets (from winc_sock.c globals)
    struct {
        uint16_t port;
        uint8_t state;
        void *handler;
    } sockets[10];
    uint8_t databuf[1600];

    // Config
    int verbose;
    bool use_crc;

    // Firmware info
    uint8_t fw_major, fw_minor, fw_patch;
    uint8_t mac[6];
} winc_ctx_t;

static winc_ctx_t g_ctx;  // Single instance

// ===== COPY ALL FUNCTIONS FROM winc_wifi.c =====
// But change pattern:
//   BEFORE: int spi_read_reg(int fd, uint32_t addr, uint32_t *valp)
//   AFTER:  static int spi_read_reg(uint32_t addr, uint32_t *valp)
//
// And inside function:
//   BEFORE: CMD_MSG_A *mp = (CMD_MSG_A *)txbuff;  // global
//   AFTER:  CMD_MSG_A *mp = (CMD_MSG_A *)g_ctx.txbuf;  // from context

// ... (copy & refactor 63 functions from winc_wifi.c)

// ===== COPY ALL FUNCTIONS FROM winc_sock.c =====
// Same pattern - remove fd parameter, use g_ctx

// ... (copy & refactor 14 functions from winc_sock.c)

// ===== PUBLIC API IMPLEMENTATION =====

bool winc_init(uint8_t node_id, const char *node_name) {
    memset(&g_ctx, 0, sizeof(g_ctx));

    // Set pins
    g_ctx.pins.sck = WINC_PIN_SCK;
    g_ctx.pins.mosi = WINC_PIN_MOSI;
    g_ctx.pins.miso = WINC_PIN_MISO;
    g_ctx.pins.cs = WINC_PIN_CS;
    g_ctx.pins.wake = WINC_PIN_WAKE;
    g_ctx.pins.reset = WINC_PIN_RESET;
    g_ctx.pins.irq = WINC_PIN_IRQ;

    // Initialize SPI (from winc_pico_part2.c:spi_setup)
    spi_init(spi0, WINC_SPI_SPEED);
    // ... (rest of SPI init)

    // Initialize chip (from winc_wifi.c)
    disable_crc();
    if (!chip_init()) return false;
    chip_get_info();  // Fills g_ctx.fw_major, etc.

    // Initialize mesh
    return winc_mesh_init(node_id, node_name);
}

void winc_poll(void) {
    // Check IRQ
    if (gpio_get(g_ctx.pins.irq) == 0) {
        interrupt_handler();
    }

    // Process mesh
    winc_mesh_process();
}

// ... other public API functions
```

**How to do it**:
1. Copy all of `winc_wifi.c` → `winc_lib.c`
2. Change every function to use `g_ctx` instead of globals
3. Make functions `static` (not public)
4. Copy all of `winc_sock.c` below that
5. Same refactoring
6. Add public API functions at end

### Step 2: Create `winc_mesh.c` (2 hours)

**Goal**: Extract mesh logic from `winc_p2p.c` and **FIX transmission**

```c
// winc_mesh.c

#include "winc_lib.h"
#include "winc_p2p.h"  // For structures

// Mesh state (part of global context, declared extern)
extern winc_ctx_t g_ctx;

// Add mesh members to context (in winc_lib.c):
typedef struct {
    // ... existing members ...

    // Mesh state
    struct {
        uint8_t my_node_id;
        char my_name[16];
        bool enabled;
        int udp_socket;  // ← IMPORTANT!

        // Routing
        struct {
            uint8_t node_id;
            uint8_t next_hop;
            uint8_t hop_count;
            uint32_t last_seen;
            bool active;
        } routes[8];
        uint8_t route_count;

        uint16_t seq_num;
        uint32_t last_beacon;

        // Callback
        void (*data_callback)(uint8_t, uint8_t*, uint16_t);
    } mesh;
} winc_ctx_t;

// ===== MESH FUNCTIONS =====

bool winc_mesh_init(uint8_t node_id, const char *name) {
    g_ctx.mesh.my_node_id = node_id;
    strncpy(g_ctx.mesh.my_name, name, sizeof(g_ctx.mesh.my_name)-1);

    // Enable P2P mode (from winc_p2p.c:p2p_enable)
    if (!p2p_enable(WINC_P2P_CHANNEL)) return false;

    // Create UDP socket for mesh communication
    g_ctx.mesh.udp_socket = open_sock_server(WINC_MESH_PORT, false,
                                              mesh_packet_handler);
    if (g_ctx.mesh.udp_socket < 0) return false;

    g_ctx.mesh.enabled = true;
    return true;
}

// ✅ FIX: Actually send beacon
static bool mesh_send_beacon(void) {
    winc_mesh_beacon_t beacon;
    memset(&beacon, 0, sizeof(beacon));

    // Build beacon
    beacon.hdr.msg_type = MESH_MSG_BEACON;
    beacon.hdr.src_node = g_ctx.mesh.my_node_id;
    beacon.hdr.dst_node = 0xFF;  // Broadcast
    beacon.hdr.seq_num = g_ctx.mesh.seq_num++;
    beacon.node_id = g_ctx.mesh.my_node_id;
    strncpy((char*)beacon.node_name, g_ctx.mesh.my_name, 15);

    // Add neighbors
    uint8_t idx = 0;
    for (int i = 0; i < g_ctx.mesh.route_count && idx < 8; i++) {
        if (g_ctx.mesh.routes[i].active &&
            g_ctx.mesh.routes[i].hop_count == 1) {
            beacon.neighbors[idx++] = g_ctx.mesh.routes[i].node_id;
        }
    }
    beacon.neighbor_count = idx;

    // ✅ ACTUALLY SEND IT!
    return put_sock_sendto(g_ctx.mesh.udp_socket, &beacon, sizeof(beacon));
}

bool winc_mesh_send(uint8_t dst_node, uint8_t *data, uint16_t len) {
    // Find route
    int next_hop = -1;
    for (int i = 0; i < g_ctx.mesh.route_count; i++) {
        if (g_ctx.mesh.routes[i].node_id == dst_node &&
            g_ctx.mesh.routes[i].active) {
            next_hop = g_ctx.mesh.routes[i].next_hop;
            break;
        }
    }
    if (next_hop < 0) return false;

    // Build packet
    struct {
        winc_mesh_hdr_t hdr;
        uint8_t data[len];
    } __attribute__((packed)) pkt;

    pkt.hdr.msg_type = MESH_MSG_DATA;
    pkt.hdr.src_node = g_ctx.mesh.my_node_id;
    pkt.hdr.dst_node = dst_node;
    pkt.hdr.hop_count = 0;
    pkt.hdr.seq_num = g_ctx.mesh.seq_num++;
    pkt.hdr.payload_len = len;
    memcpy(pkt.data, data, len);

    // ✅ ACTUALLY SEND IT!
    return put_sock_sendto(g_ctx.mesh.udp_socket, &pkt, sizeof(pkt));
}

void winc_mesh_process(void) {
    uint32_t now = to_ms_since_boot(get_absolute_time());

    // Send beacon periodically
    if (now - g_ctx.mesh.last_beacon > WINC_MESH_BEACON_INTERVAL_MS) {
        mesh_send_beacon();
        g_ctx.mesh.last_beacon = now;
    }

    // Timeout old routes
    for (int i = 0; i < g_ctx.mesh.route_count; i++) {
        if (g_ctx.mesh.routes[i].active &&
            (now - g_ctx.mesh.routes[i].last_seen > WINC_MESH_ROUTE_TIMEOUT_MS)) {
            g_ctx.mesh.routes[i].active = false;
        }
    }
}

// Packet handler (called by socket layer)
static void mesh_packet_handler(uint8_t sock, int rxlen) {
    uint8_t buf[1600];
    if (rxlen <= 0 || !get_sock_data(sock, buf, rxlen)) return;

    winc_mesh_hdr_t *hdr = (winc_mesh_hdr_t*)buf;

    switch (hdr->msg_type) {
        case MESH_MSG_BEACON:
            mesh_handle_beacon((winc_mesh_beacon_t*)buf);
            break;

        case MESH_MSG_DATA:
            if (hdr->dst_node == g_ctx.mesh.my_node_id) {
                // For us!
                if (g_ctx.mesh.data_callback) {
                    g_ctx.mesh.data_callback(hdr->src_node,
                                             buf + sizeof(winc_mesh_hdr_t),
                                             hdr->payload_len);
                }
            } else {
                // Route it
                mesh_route_packet((winc_mesh_hdr_t*)buf,
                                  buf + sizeof(winc_mesh_hdr_t));
            }
            break;
    }
}

// ... rest of mesh functions
```

### Step 3: Test (30 min)

```bash
cd build
cmake -DMY_NODE_ID=1 ..
make mesh_node
# Flash to Pico 1

cmake -DMY_NODE_ID=2 ..
make mesh_node
# Flash to Pico 2

# Watch serial output
screen /dev/ttyACM0 115200
```

---

## 📋 Refactoring Checklist

### For Each Function in `winc_wifi.c` and `winc_sock.c`:

- [ ] Remove `int fd` parameter
- [ ] Change `txbuff` → `g_ctx.txbuf`
- [ ] Change `rxbuff` → `g_ctx.rxbuf`
- [ ] Change `verbose` → `g_ctx.verbose`
- [ ] Change `spi_fd` → (use spi0 directly)
- [ ] Make function `static` if not in public API
- [ ] Test compilation

### For Mesh (`winc_p2p.c`):

- [ ] Extract beacon/routing logic
- [ ] Add `g_ctx.mesh.udp_socket` member
- [ ] Create UDP socket in `winc_mesh_init()`
- [ ] Fix `mesh_send_beacon()` to call `put_sock_sendto()`
- [ ] Fix `mesh_send_data()` to call `put_sock_sendto()`
- [ ] Add `mesh_packet_handler()` for received packets
- [ ] Test packet transmission with sniffer

---

## 🧪 Testing Strategy

### Level 1: Compilation
```bash
make mesh_node
# Should compile without errors
```

### Level 2: Initialization
```c
if (winc_init(1, "Test")) {
    printf("Init OK\n");
    winc_get_firmware_version(&maj, &min, &pat);
    printf("FW: %u.%u.%u\n", maj, min, pat);
}
```

### Level 3: P2P Mode
```c
// Should see P2P mode enabled
// Check with WiFi sniffer on channel 1
```

### Level 4: Beacons
```c
// Use Wireshark with WiFi adapter in monitor mode
// Filter: wlan.fc.type_subtype == 0x08  (beacon frames)
// Should see beacons every 5 seconds
```

### Level 5: Mesh Communication
```c
// Two boards:
// Board 1: winc_mesh_send(2, "test", 4);
// Board 2: Should receive in callback
```

---

## 🎯 Success Criteria

- [ ] `mesh_node` compiles without warnings
- [ ] Firmware version prints correctly
- [ ] P2P mode enables
- [ ] Beacons transmit (verify with sniffer)
- [ ] Two nodes discover each other
- [ ] Routing table populated
- [ ] Data packets transmit
- [ ] Callback receives data
- [ ] Multi-hop routing works (3+ nodes)

---

## 📚 Reference Materials

### Original Code Locations

**SPI/HIF Functions** (`winc_wifi.c`):
- Lines 172-265: SPI operations
- Lines 302-353: Chip init
- Lines 356-416: HIF operations

**Socket Functions** (`winc_sock.c`):
- Lines 43-60: Socket management
- Lines 63-121: Interrupt handler
- Lines 183-247: Socket operations

**P2P/Mesh** (`winc_p2p.c`):
- Lines 29-77: P2P enable/disable
- Lines 169-218: Mesh init
- Lines 220-262: Beacon send (NEEDS FIX)
- Lines 265-301: Data send (NEEDS FIX)
- Lines 346-387: Routing table update

### Key Fixes

1. **Line 221 (`winc_p2p.c`)**: `mesh_send_beacon()` doesn't send
   - **Fix**: Call `put_sock_sendto(g_ctx.mesh.udp_socket, &beacon, sizeof(beacon))`

2. **Line 266 (`winc_p2p.c`)**: `mesh_send_data()` doesn't send
   - **Fix**: Call `put_sock_sendto(g_ctx.mesh.udp_socket, &pkt, sizeof(pkt))`

3. **Missing socket**: No UDP socket created for mesh
   - **Fix**: Call `open_sock_server(WINC_MESH_PORT, false, mesh_packet_handler)` in `winc_mesh_init()`

---

## ⏱️ Time Estimate

| Task | Time | Notes |
|------|------|-------|
| Merge winc_wifi.c → winc_lib.c | 2h | Mechanical refactoring |
| Merge winc_sock.c → winc_lib.c | 1h | Mechanical refactoring |
| Create winc_mesh.c | 1.5h | Extract + fix transmission |
| Test & debug | 0.5h | Hardware testing |
| **TOTAL** | **5h** | Focused work |

---

## 🚀 Ready to Start?

1. **Read** [SIMPLIFIED_DESIGN.md](SIMPLIFIED_DESIGN.md) for overall design
2. **Start** with Step 1 (create `winc_lib.c`)
3. **Pattern**: Change all functions from globals to `g_ctx`
4. **Test** after each file
5. **Fix** mesh transmission in Step 2
6. **Test** with multiple boards

---

**You now have everything you need to implement the library!**

The design is simple, focused, and ready for P2P mesh networking on Pico/Pico 2.
