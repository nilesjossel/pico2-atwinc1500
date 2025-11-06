# WINC1500 Library Implementation Roadmap

## Overview

This document provides a step-by-step guide to complete the refactoring of the WINC1500 WiFi driver into a reusable library suitable for integration into other projects.

---

## Current Status

### ✅ Completed
1. **Analysis** - Comprehensive efficiency analysis ([EFFICIENCY_ANALYSIS.md](EFFICIENCY_ANALYSIS.md))
2. **Architecture Design** - Context-based architecture defined
3. **Directory Structure** - Library folder structure created
4. **Public API** - Headers created:
   - `lib/include/winc_types.h` - Type definitions and enums
   - `lib/include/winc_mesh.h` - Public API functions

### 🚧 In Progress
- Implementation of context structure
- Refactoring existing code to use context

### ⏳ Not Started
- CMakeLists.txt update
- Example applications
- Testing
- Documentation

---

## Implementation Steps

### Step 1: Create Context Structure (PRIORITY: HIGH)

**File**: `lib/src/winc_context.h`

```c
#ifndef WINC_CONTEXT_H
#define WINC_CONTEXT_H

#include <stdint.h>
#include <stdbool.h>
#include "winc_types.h"

#define SPI_BUFFLEN 1600
#define MAX_SOCKETS 10

// Forward declarations (these will be fully defined in private headers)
typedef struct socket_t SOCKET;
typedef struct mesh_node_t MESH_NODE;
typedef struct mesh_routing_table_t MESH_ROUTING_TABLE;

// Main library context - replaces ALL global variables
struct winc_context {
    // ===== Hardware Configuration =====
    uint8_t sck_pin, mosi_pin, miso_pin, cs_pin;
    uint8_t wake_pin, reset_pin, irq_pin;
    uint32_t spi_speed;
    void *spi_port;  // Platform-specific (spi0, spi1, etc.)

    // ===== SPI Buffers (previously global in winc_wifi.c) =====
    uint8_t txbuff[SPI_BUFFLEN];
    uint8_t rxbuff[SPI_BUFFLEN];
    uint8_t tx_zeros[1024];

    // ===== Socket Layer (previously global in winc_sock.c) =====
    SOCKET sockets[MAX_SOCKETS];
    uint8_t databuff[SPI_BUFFLEN];

    // ===== P2P/Mesh State (previously global in winc_p2p.c) =====
    bool p2p_enabled;
    bool mesh_enabled;
    uint8_t p2p_mode;
    MESH_ROUTING_TABLE routing_table;
    uint16_t mesh_seq_num;
    char local_node_name[16];
    uint32_t last_beacon_time;
    int mesh_udp_socket;  // Socket for mesh communication
    int mesh_tcp_socket;  // Optional TCP socket

    // ===== WiFi State =====
    WINC_WIFI_STATUS wifi_status;

    // ===== Configuration =====
    int verbose;
    bool use_crc;

    // ===== Callbacks =====
    winc_mesh_data_fn mesh_data_handler;
    void *mesh_user_data;
    winc_wifi_status_fn wifi_status_handler;
    void *wifi_user_data;

    // ===== Firmware Info =====
    uint8_t fw_major, fw_minor, fw_patch;
    uint8_t mac_address[6];
    uint32_t chip_id;
};

// Initialization functions
WINC_CONTEXT* winc_create_context(void);
void winc_destroy_context(WINC_CONTEXT *ctx);

#endif // WINC_CONTEXT_H
```

**File**: `lib/src/winc_context.c`

```c
#include <stdlib.h>
#include <string.h>
#include "winc_context.h"

WINC_CONTEXT* winc_create_context(void)
{
    WINC_CONTEXT *ctx = (WINC_CONTEXT*)malloc(sizeof(WINC_CONTEXT));
    if (ctx) {
        memset(ctx, 0, sizeof(WINC_CONTEXT));
        ctx->mesh_udp_socket = -1;
        ctx->mesh_tcp_socket = -1;
    }
    return ctx;
}

void winc_destroy_context(WINC_CONTEXT *ctx)
{
    if (ctx) {
        // TODO: Close all sockets, disable P2P/mesh, etc.
        free(ctx);
    }
}
```

---

### Step 2: Refactor SPI Layer (PRIORITY: HIGH)

**Goal**: Convert `winc_wifi.c` → `lib/src/winc_spi.c` with context

**Pattern**: Add `WINC_CONTEXT *ctx` as first parameter to ALL functions

**Example Refactoring**:

**BEFORE** (`winc_wifi.c:178`):
```c
int spi_read_reg(int fd, uint32_t addr, uint32_t *valp)
{
    CMD_MSG_A *mp=(CMD_MSG_A *)txbuff;  // Global!
    // ...
    if (verbose > 1)  // Global!
        printf("Rd reg %04x: %08x\n", addr, *valp);
    return(rxlen);
}
```

**AFTER** (`lib/src/winc_spi.c`):
```c
int spi_read_reg(WINC_CONTEXT *ctx, uint32_t addr, uint32_t *valp)
{
    CMD_MSG_A *mp=(CMD_MSG_A *)ctx->txbuff;  // From context!
    // ...
    if (ctx->verbose > 1)  // From context!
        printf("Rd reg %04x: %08x\n", addr, *valp);
    return(rxlen);
}
```

**Functions to Refactor** (63 total in winc_wifi.c):
- `spi_cmd_resp()` → needs `ctx`
- `spi_read_reg()` → needs `ctx`
- `spi_write_reg()` → needs `ctx`
- `spi_read_data()` → needs `ctx`
- `spi_write_data()` → needs `ctx`
- `disable_crc()` → needs `ctx`
- `chip_init()` → needs `ctx`
- `chip_get_info()` → needs `ctx`
- `chip_get_id()` → needs `ctx`
- `hif_start()` → needs `ctx`
- `hif_put()` → needs `ctx`
- `hif_get()` → needs `ctx`
- `hif_rx_done()` → needs `ctx`
- `join_net()` → needs `ctx`
- ... (all others)

**Important**: Also remove `int fd` parameter - it's now `ctx->spi_fd` (or handle via platform layer)

---

### Step 3: Refactor Socket Layer (PRIORITY: HIGH)

**Goal**: Convert `winc_sock.c` → `lib/src/winc_socket.c`

**Pattern**: Same as SPI layer

**Example**:

**BEFORE** (`winc_sock.c:43`):
```c
int open_sock_server(int portnum, bool tcp, SOCK_HANDLER handler)
{
    // Accesses global: sockets[sock]
}
```

**AFTER**:
```c
int open_sock_server(WINC_CONTEXT *ctx, int portnum, bool tcp, SOCK_HANDLER handler)
{
    SOCKET *sockets = ctx->sockets;
    // ... rest of logic unchanged
}
```

**Functions to Refactor** (14 total):
- `open_sock_server()` → needs `ctx`
- `interrupt_handler()` → needs `ctx`
- `check_sock()` → needs `ctx`
- `sock_state()` → needs `ctx`
- `put_sock_bind()` → needs `ctx`
- `put_sock_listen()` → needs `ctx`
- `put_sock_recv()` → needs `ctx`
- `put_sock_recvfrom()` → needs `ctx`
- `put_sock_send()` → needs `ctx`
- `put_sock_sendto()` → needs `ctx`
- `put_sock_close()` → needs `ctx`
- `get_sock_data()` → needs `ctx`
- `tcp_echo_handler()` → move to examples/ (not library code)
- `udp_echo_handler()` → move to examples/ (not library code)

---

### Step 4: Refactor P2P/Mesh Layer (PRIORITY: CRITICAL)

**Goal**: Convert `winc_p2p.c` → `lib/src/winc_p2p.c` AND FIX TRANSMISSION

**Pattern**: Same context refactoring + fix mesh_send functions

**CRITICAL FIX** - mesh_send_beacon():

**BEFORE** (`winc_p2p.c:221`):
```c
bool mesh_send_beacon(int fd)
{
    MESH_BEACON beacon;
    // ... builds beacon ...

    // THIS DOES NOTHING:
    // Note: This uses UDP broadcast on the P2P network
    // You'll need to set up a UDP socket for mesh communication

    last_beacon_time = time_us_32() / 1000;
    return true;  // ❌ DOESN'T ACTUALLY SEND!
}
```

**AFTER**:
```c
WINC_ERROR mesh_send_beacon(WINC_CONTEXT *ctx)
{
    MESH_BEACON beacon;

    if (!ctx->mesh_enabled)
        return WINC_ERR_MESH_DISABLED;

    memset(&beacon, 0, sizeof(beacon));

    // Fill beacon header
    beacon.hdr.msg_type = MESH_MSG_BEACON;
    beacon.hdr.src_node = ctx->routing_table.local_node_id;
    beacon.hdr.dst_node = 0xFF; // Broadcast
    beacon.hdr.hop_count = 0;
    beacon.hdr.seq_num = ctx->mesh_seq_num++;
    beacon.hdr.payload_len = sizeof(MESH_BEACON) - sizeof(MESH_PKT_HDR);

    // Fill beacon data
    beacon.node_id = ctx->routing_table.local_node_id;
    strncpy((char *)beacon.node_name, ctx->local_node_name, sizeof(beacon.node_name) - 1);

    // Add neighbors
    uint8_t neighbor_idx = 0;
    for (uint8_t i = 0; i < ctx->routing_table.node_count && neighbor_idx < MESH_MAX_NODES; i++)
    {
        if (ctx->routing_table.nodes[i].is_active &&
            ctx->routing_table.nodes[i].hop_count == 1)
        {
            beacon.neighbors[neighbor_idx++] = ctx->routing_table.nodes[i].node_id;
        }
    }
    beacon.neighbor_count = neighbor_idx;

    if (ctx->verbose > 1)
        printf("Sending mesh beacon, neighbors: %u\n", beacon.neighbor_count);

    // ✅ ACTUALLY SEND THE BEACON!
    bool ok = put_sock_sendto(ctx, ctx->mesh_udp_socket, &beacon, sizeof(beacon));

    if (ok) {
        ctx->last_beacon_time = time_us_32() / 1000;
        return WINC_OK;
    }

    return WINC_ERR_SPI;  // Or more specific error
}
```

**CRITICAL FIX** - mesh_send_data():

**BEFORE** (`winc_p2p.c:266`):
```c
bool mesh_send_data(int fd, uint8_t dst_node, uint8_t *data, uint16_t len)
{
    // ... finds route ...

    // Send packet to next hop
    // This would use the socket interface to send to the next hop node

    return true;  // ❌ DOESN'T SEND!
}
```

**AFTER**:
```c
WINC_ERROR mesh_send_data(WINC_CONTEXT *ctx, uint8_t dst_node, uint8_t *data, uint16_t len)
{
    if (!ctx->mesh_enabled)
        return WINC_ERR_MESH_DISABLED;

    // Find route
    int next_hop = mesh_find_route(ctx, dst_node);
    if (next_hop < 0)
        return WINC_ERR_NO_ROUTE;

    // Build packet
    uint8_t packet[sizeof(MESH_PKT_HDR) + len];
    MESH_PKT_HDR *hdr = (MESH_PKT_HDR*)packet;
    hdr->msg_type = MESH_MSG_DATA;
    hdr->src_node = ctx->routing_table.local_node_id;
    hdr->dst_node = dst_node;
    hdr->hop_count = 0;
    hdr->seq_num = ctx->mesh_seq_num++;
    hdr->payload_len = len;
    memcpy(packet + sizeof(MESH_PKT_HDR), data, len);

    if (ctx->verbose)
        printf("Sending mesh data to node %u via next hop %d\n", dst_node, next_hop);

    // ✅ ACTUALLY SEND THE PACKET!
    bool ok = put_sock_sendto(ctx, ctx->mesh_udp_socket, packet, sizeof(packet));

    return ok ? WINC_OK : WINC_ERR_SPI;
}
```

**Functions to Refactor** (17 total):
- All P2P/mesh functions need `ctx`
- `mesh_send_beacon()` - FIX transmission
- `mesh_send_data()` - FIX transmission
- `mesh_route_packet()` - needs `ctx`
- `mesh_update_routing_table()` - needs `ctx`
- `mesh_find_route()` - needs `ctx`
- ... (all others)

---

### Step 5: Implement Public API (PRIORITY: HIGH)

**File**: `lib/src/winc_api.c`

Implement all functions declared in `lib/include/winc_mesh.h`:

```c
#include "winc_mesh.h"
#include "winc_context.h"
#include "winc_private.h"  // Internal functions

WINC_CONTEXT* winc_init(const WINC_GPIO_CONFIG *gpio, const WINC_CONFIG *cfg)
{
    if (!gpio || !cfg)
        return NULL;

    WINC_CONTEXT *ctx = winc_create_context();
    if (!ctx)
        return NULL;

    // Copy configuration
    ctx->sck_pin = gpio->sck_pin;
    ctx->mosi_pin = gpio->mosi_pin;
    ctx->miso_pin = gpio->miso_pin;
    ctx->cs_pin = gpio->cs_pin;
    ctx->wake_pin = gpio->wake_pin;
    ctx->reset_pin = gpio->reset_pin;
    ctx->irq_pin = gpio->irq_pin;
    ctx->spi_speed = gpio->spi_speed;
    ctx->verbose = cfg->verbose_level;

    // Initialize hardware (platform-specific)
    spi_setup(ctx);  // Needs to be refactored to use ctx

    // Initialize chip
    disable_crc(ctx);
    if (!chip_init(ctx)) {
        winc_destroy_context(ctx);
        return NULL;
    }

    chip_get_info(ctx);  // Populates ctx->fw_major, etc.

    return ctx;
}

void winc_deinit(WINC_CONTEXT *ctx)
{
    if (!ctx)
        return;

    // Cleanup
    if (ctx->mesh_enabled)
        mesh_disable(ctx);

    if (ctx->p2p_enabled)
        p2p_disable(ctx);

    // Close all sockets
    for (int i = 0; i < MAX_SOCKETS; i++) {
        if (ctx->sockets[i].state != STATE_CLOSED)
            put_sock_close(ctx, i);
    }

    winc_destroy_context(ctx);
}

void winc_poll(WINC_CONTEXT *ctx)
{
    if (!ctx)
        return;

    // Check for interrupts
    if (read_irq() == 0)  // Note: read_irq needs platform abstraction
        interrupt_handler(ctx);

    // Handle mesh beacons if enabled
    if (ctx->mesh_enabled)
        mesh_beacon_handler(ctx);
}

WINC_ERROR winc_mesh_start(WINC_CONTEXT *ctx, uint8_t node_id, const char *name, uint8_t channel)
{
    if (!ctx || !name)
        return WINC_ERR_INVALID_PARAM;

    // Enable P2P mode first
    if (!p2p_enable(ctx, channel))
        return WINC_ERR_INIT;

    // Initialize mesh
    if (!mesh_init(ctx, node_id, (char*)name))
        return WINC_ERR_INIT;

    // Enable mesh (this creates the UDP socket)
    if (!mesh_enable(ctx))
        return WINC_ERR_INIT;

    return WINC_OK;
}

WINC_ERROR winc_mesh_send(WINC_CONTEXT *ctx, uint8_t dst_node, const uint8_t *data, uint16_t len)
{
    if (!ctx || !data)
        return WINC_ERR_INVALID_PARAM;

    return mesh_send_data(ctx, dst_node, (uint8_t*)data, len);
}

// ... implement all other public API functions
```

---

### Step 6: Update CMakeLists.txt (PRIORITY: MEDIUM)

**File**: `CMakeLists.txt` (root)

```cmake
cmake_minimum_required(VERSION 3.13)

set(PICO_SDK_PATH "$ENV{PICO_SDK_PATH}")
set(PICO_BOARD pico2)

include(pico_sdk_import.cmake)
project(WINC1500_PICO2 C CXX ASM)
pico_sdk_init()

# ===== WINC1500 Library =====
add_library(winc1500 STATIC
    lib/src/winc_context.c
    lib/src/winc_spi.c         # Refactored from winc_wifi.c
    lib/src/winc_socket.c      # Refactored from winc_sock.c
    lib/src/winc_p2p.c         # Refactored
    lib/src/winc_api.c         # Public API implementation
    lib/src/winc_platform_pico.c  # Platform-specific code
)

target_include_directories(winc1500 PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/lib/include
)

target_include_directories(winc1500 PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/lib/src
)

target_link_libraries(winc1500
    pico_stdlib
    hardware_spi
)

# ===== Examples =====
add_subdirectory(examples)

# ===== Original test program (for compatibility) =====
add_executable(WINC1500_PICO2 WINC1500_PICO2.c)
target_link_libraries(WINC1500_PICO2 pico_stdlib hardware_spi)
pico_enable_stdio_uart(WINC1500_PICO2 1)
pico_enable_stdio_usb(WINC1500_PICO2 1)
pico_add_extra_outputs(WINC1500_PICO2)
```

**File**: `examples/CMakeLists.txt`

```cmake
# Mesh Node Example
add_executable(mesh_node mesh_node.c)
target_link_libraries(mesh_node winc1500)
pico_enable_stdio_uart(mesh_node 1)
pico_enable_stdio_usb(mesh_node 1)
pico_add_extra_outputs(mesh_node)

# WiFi Client Example
add_executable(wifi_client wifi_client.c)
target_link_libraries(wifi_client winc1500)
pico_enable_stdio_uart(wifi_client 1)
pico_enable_stdio_usb(wifi_client 1)
pico_add_extra_outputs(wifi_client)
```

---

### Step 7: Create Example Applications (PRIORITY: MEDIUM)

**File**: `examples/mesh_node.c`

```c
#include <stdio.h>
#include "pico/stdlib.h"
#include "winc_mesh.h"

void my_mesh_handler(uint8_t src, uint8_t *data, uint16_t len, void *user_data)
{
    printf("Mesh data from node %u: %.*s\n", src, len, data);
}

int main()
{
    // Configure GPIO
    WINC_GPIO_CONFIG gpio = {
        .sck_pin = 18, .mosi_pin = 19, .miso_pin = 16, .cs_pin = 17,
        .wake_pin = 20, .reset_pin = 21, .irq_pin = 22,
        .spi_speed = 11000000
    };

    WINC_CONFIG cfg = { .verbose_level = 1 };

    // Initialize library
    WINC_CONTEXT *ctx = winc_init(&gpio, &cfg);
    if (!ctx) {
        printf("Failed to init\n");
        return -1;
    }

    // Start mesh network (Node 1, Channel 1)
    if (winc_mesh_start(ctx, 1, "Node1", 1) != WINC_OK) {
        printf("Mesh start failed\n");
        return -1;
    }

    winc_mesh_set_handler(ctx, my_mesh_handler, NULL);

    printf("Mesh node started. Press Ctrl+C to exit.\n");

    uint32_t last_print = 0;
    while (true) {
        // Poll for events (handles IRQ, beacons, routing)
        winc_poll(ctx);

        // Print routing table every 30s
        uint32_t now = to_ms_since_boot(get_absolute_time());
        if (now - last_print > 30000) {
            winc_mesh_print_routes(ctx);
            last_print = now;
        }
    }

    winc_deinit(ctx);
    return 0;
}
```

---

## Testing Checklist

- [ ] Library compiles without warnings
- [ ] No global variables remain (check with `nm` or `objdump`)
- [ ] Mesh beacons are actually sent (verify with Wireshark or sniffer)
- [ ] Mesh data transmission works between nodes
- [ ] Routing table updates correctly
- [ ] WiFi connection works in standard mode
- [ ] Sockets work correctly
- [ ] No memory leaks (run with Valgrind if possible)
- [ ] Example applications work as expected

---

## Estimated Timeline

| Phase | Task | Effort | Dependencies |
|-------|------|--------|--------------|
| 1 | Create context structure | 2 hours | None |
| 2 | Refactor SPI layer | 4 hours | Phase 1 |
| 3 | Refactor socket layer | 3 hours | Phase 1 |
| 4 | Refactor P2P/mesh + FIX transmission | 5 hours | Phases 1-3 |
| 5 | Implement public API | 4 hours | Phases 1-4 |
| 6 | Update CMakeLists.txt | 1 hour | Phase 5 |
| 7 | Create examples | 2 hours | Phase 6 |
| 8 | Testing & debugging | 4 hours | Phase 7 |
| **TOTAL** | **25 hours (~3-4 days)** |  |  |

---

## Common Pitfalls

1. **Forgetting to update function calls**: Use global search/replace carefully
2. **Not initializing context**: Add `memset(ctx, 0, sizeof(*ctx))` in `winc_create_context()`
3. **Mesh socket not created**: Ensure `mesh_enable()` creates UDP socket
4. **IRQ handling**: Platform abstraction needed for `read_irq()`
5. **Circular dependencies**: Keep private headers separate from public API

---

## Next Steps

1. Start with Step 1 (context structure) - this is the foundation
2. Then proceed to Step 2 (SPI layer) - this is the largest refactoring
3. Don't forget Step 4 (fix mesh transmission) - this is critical for functionality
4. Test incrementally after each phase

---

## Support

For questions or issues:
1. Review [EFFICIENCY_ANALYSIS.md](EFFICIENCY_ANALYSIS.md) for architectural details
2. Check original documentation at https://iosoft.blog/winc_wifi
3. Refer to ATWINC1500 datasheet: https://documentation.help/WINC1500/modules.html

**Good luck with the implementation!**
