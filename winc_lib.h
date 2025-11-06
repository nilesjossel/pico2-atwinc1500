// ATWINC1500 WiFi Library - Simple P2P Mesh Networking
// For Raspberry Pi Pico / Pico 2
//
// Copyright (c) 2021 Jeremy P Bentham (original winc_wifi code)
// Copyright (c) 2025 Niles Roxas (mesh networking & simplification)
// Licensed under the Apache License, Version 2.0

#ifndef WINC_LIB_H
#define WINC_LIB_H

#include <stdint.h>
#include <stdbool.h>
#include "pico/stdlib.h"

// ============================================================================
// CONFIGURATION (can override with -D flags at compile time)
// ============================================================================

#ifndef WINC_PIN_SCK
#define WINC_PIN_SCK    18
#endif

#ifndef WINC_PIN_MOSI
#define WINC_PIN_MOSI   19
#endif

#ifndef WINC_PIN_MISO
#define WINC_PIN_MISO   16
#endif

#ifndef WINC_PIN_CS
#define WINC_PIN_CS     17
#endif

#ifndef WINC_PIN_WAKE
#define WINC_PIN_WAKE   20
#endif

#ifndef WINC_PIN_RESET
#define WINC_PIN_RESET  21
#endif

#ifndef WINC_PIN_IRQ
#define WINC_PIN_IRQ    22
#endif

#ifndef WINC_SPI_SPEED
#define WINC_SPI_SPEED  11000000  // 11 MHz
#endif

#ifndef WINC_P2P_CHANNEL
#define WINC_P2P_CHANNEL 1  // P2P channel (1, 6, or 11)
#endif

#ifndef WINC_MESH_PORT
#define WINC_MESH_PORT  1025  // UDP port for mesh communication
#endif

#ifndef WINC_MESH_BEACON_INTERVAL_MS
#define WINC_MESH_BEACON_INTERVAL_MS  5000  // 5 seconds
#endif

#ifndef WINC_MESH_ROUTE_TIMEOUT_MS
#define WINC_MESH_ROUTE_TIMEOUT_MS    30000  // 30 seconds
#endif

#ifndef WINC_MESH_MAX_NODES
#define WINC_MESH_MAX_NODES 8
#endif

// ============================================================================
// PUBLIC API
// ============================================================================

/**
 * Initialize WINC1500 and start mesh network
 *
 * @param node_id Unique node ID (1-255)
 * @param node_name Human-readable name (max 15 chars)
 * @return true on success, false on failure
 *
 * Example:
 *   if (!winc_init(1, "Pico1")) {
 *       printf("WINC init failed\n");
 *       return -1;
 *   }
 */
bool winc_init(uint8_t node_id, const char *node_name);

/**
 * Poll for events - CALL THIS IN YOUR MAIN LOOP
 *
 * Handles:
 * - Interrupt processing
 * - Beacon sending
 * - Route maintenance
 * - Data reception
 *
 * Example:
 *   while (1) {
 *       winc_poll();
 *       // Your application code
 *   }
 */
void winc_poll(void);

/**
 * Set callback for received mesh data
 *
 * @param callback Function to call when data received
 *                 Parameters: src_node, data, len
 *
 * Example:
 *   void my_handler(uint8_t src, uint8_t *data, uint16_t len) {
 *       printf("From %u: %.*s\n", src, len, data);
 *   }
 *   winc_mesh_set_callback(my_handler);
 */
void winc_mesh_set_callback(void (*callback)(uint8_t src_node, uint8_t *data, uint16_t len));

/**
 * Send data to another mesh node
 *
 * @param dst_node Destination node ID
 * @param data Data buffer to send
 * @param len Length of data (max ~1400 bytes)
 * @return true if sent, false if no route or error
 *
 * Example:
 *   char msg[] = "Hello from node 1!";
 *   if (winc_mesh_send(2, (uint8_t*)msg, strlen(msg))) {
 *       printf("Sent to node 2\n");
 *   } else {
 *       printf("No route to node 2\n");
 *   }
 */
bool winc_mesh_send(uint8_t dst_node, uint8_t *data, uint16_t len);

/**
 * Print routing table to stdout (for debugging)
 *
 * Example output:
 *   Mesh Routing Table (Node 1 - "Pico1"):
 *     Node 2: 1 hop (direct)
 *     Node 3: 2 hops via node 2
 */
void winc_mesh_print_routes(void);

/**
 * Get number of active routes
 *
 * @return Number of nodes in routing table
 */
uint8_t winc_mesh_get_node_count(void);

/**
 * Set verbose level
 *
 * @param level 0=silent, 1=info, 2=debug, 3=trace
 */
void winc_set_verbose(int level);

/**
 * Get firmware version
 *
 * @param major Output: major version
 * @param minor Output: minor version
 * @param patch Output: patch version
 */
void winc_get_firmware_version(uint8_t *major, uint8_t *minor, uint8_t *patch);

/**
 * Get MAC address
 *
 * @param mac Output: 6-byte MAC address
 */
void winc_get_mac(uint8_t mac[6]);

/**
 * Get local node ID
 *
 * @return Node ID set during winc_init()
 */
uint8_t winc_get_node_id(void);

/**
 * Get local node name
 *
 * @return Node name set during winc_init()
 */
const char* winc_get_node_name(void);

// ============================================================================
// INTERNAL TYPES (for advanced users)
// ============================================================================

// Mesh packet header
typedef struct __attribute__((packed)) {
    uint8_t msg_type;      // MESH_MSG_*
    uint8_t src_node;
    uint8_t dst_node;
    uint8_t hop_count;
    uint16_t seq_num;
    uint16_t payload_len;
} winc_mesh_hdr_t;

// Mesh message types
#define MESH_MSG_BEACON     0x01
#define MESH_MSG_DATA       0x02
#define MESH_MSG_ROUTE_REQ  0x03
#define MESH_MSG_ROUTE_RESP 0x04

// Mesh beacon packet
typedef struct __attribute__((packed)) {
    winc_mesh_hdr_t hdr;
    uint8_t node_id;
    uint8_t node_name[16];
    uint8_t neighbors[WINC_MESH_MAX_NODES];
    uint8_t neighbor_count;
} winc_mesh_beacon_t;

// Routing table entry
typedef struct {
    uint8_t node_id;
    uint8_t next_hop;
    uint8_t hop_count;
    uint32_t last_seen;
    bool active;
} winc_route_t;

// ===== WINC TYPE DEFINITIONS (from winc_wifi.h and winc_sock.h) =====
#ifndef MIN
#define MIN(a,b) (((a)<(b))?(a):(b))
#define MAX(a,b) (((a)>(b))?(a):(b))
#endif

#define IP_BYTES(x) x&255, x>>8&255, x>>16&255, x>>24&255

#define CHIPID_REG          0x1000
#define EFUSE_REG           0x1014
#define RCV_CTRL_REG3       0x106c
#define RCV_CTRL_REG0       0x1070
#define RCV_CTRL_REG2       0x1078
#define RCV_CTRL_REG1       0x1084
#define NMI_STATE_REG       0x108c
#define REVID_REG           0x13f4
#define PIN_MUX_REG0        0x1408
#define NMI_GP_REG1         0x14a0
#define NMI_EN_REG          0x1a00
#define HOST_WAIT_REG      0x207bc
#define NMI_GP_REG2        0xc0008
#define BOOTROM_REG        0xc000c
#define RCV_CTRL_REG4     0x150400

#define CMD_SINGLE_WRITE    0xc9
#define CMD_SINGLE_READ     0xca
#define CMD_WRITE_DATA      0xc7
#define CMD_READ_DATA       0xc8
#define CMD_INTERNAL_READ   0xc4

#define GID_WIFI        1
#define GID_IP          2
#define GIDOP(gid, op) ((gid << 8) | op)
#define GOP_CONN_REQ_OLD    GIDOP(GID_WIFI, 40)
#define GOP_STATE_CHANGE    GIDOP(GID_WIFI, 44)
#define GOP_DHCP_CONF       GIDOP(GID_WIFI, 50)
#define GOP_CONN_REQ_NEW    GIDOP(GID_WIFI, 59)
#define GOP_BIND            GIDOP(GID_IP,   65)
#define GOP_LISTEN          GIDOP(GID_IP,   66)
#define GOP_ACCEPT          GIDOP(GID_IP,   67)
#define GOP_SEND            GIDOP(GID_IP,   69)
#define GOP_RECV            GIDOP(GID_IP,   70)
#define GOP_SENDTO          GIDOP(GID_IP,   71)
#define GOP_RECVFROM        GIDOP(GID_IP,   72)
#define GOP_CLOSE           GIDOP(GID_IP,   73)

#define HIF_HDR_SIZE        8
#define ANY_CHAN        255
#define AUTH_OPEN       1
#define AUTH_PSK        2
#define CRED_NO_STORE   0
#define CRED_STORE      3
#define REQ_DATA        0x80

// Socket definitions
#define MIN_SOCKET      0
#define MIN_TCP_SOCK    0
#define MAX_TCP_SOCK    7
#define MIN_UDP_SOCK    7
#define MAX_UDP_SOCK    10
#define MAX_SOCKETS     10
#define IP_FAMILY       2
#define STATE_CLOSED    0
#define STATE_BINDING   1
#define STATE_BOUND     2
#define STATE_ACCEPTED  3
#define STATE_CONNECTED 4
#define UDP_DATA_OSET       68
#define TCP_DATA_OSET       80

// Utility macros
#define NEW_JOIN            0
#define U16_DATA(d, n, val) {d[n]=val>>8; d[n+1]=val;}
#define U24_DATA(d, n, val) {d[n]=val>>16; d[n+1]=val>>8; d[n+2]=val;}
#define U32_DATA(d, n, val) {d[n]=val>>24; d[n+1]=val>>16; d[n+2]=val>>8; d[n+3]=val;}
#define DATA_U32(d)         ((d[0]<<24) | (d[1]<<16) | (d[2]<<8) | d[3])
#define RSP_U32(d, n)       (d[n] | (uint32_t)(d[n+1])<<8 | (uint32_t)(d[n+2])<<16 | (uint32_t)(d[n+3])<<24)
#define CLOCKLESS_ADDR      (1 << 15)

#define FINISH_BOOT_VAL 0x10add09e
#define DRIVER_VER_INFO 0x13521330
#define CONF_VAL             0x102
#define START_FIRMWARE  0xef522f61
#define FINISH_INIT_VAL 0x02532636


#endif // WINC_LIB_H
