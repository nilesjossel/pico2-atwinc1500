// ATWINC1500 Mesh Networking Implementation
// For Raspberry Pi Pico / Pico 2

// Mesh networking implementation
// Extracted and fixed from winc_p2p.c

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include "pico/stdlib.h"
#include "winc_lib.h"

// Forward declarations for functions from winc_lib.c
// Note: winc_lib.c has global context, these functions don't need fd param

// P2P operation codes (from winc_lib.c internal defines)
#define GID_WIFI        1
#define GID_IP          2
#define GIDOP(gid, op) ((gid << 8) | op)
#define GOP_P2P_ENABLE      GIDOP(GID_WIFI, 85)
#define GOP_P2P_DISABLE     GIDOP(GID_WIFI, 86)

// P2P enable command
typedef struct {
    uint8_t channel;
} P2P_ENABLE_CMD;

// Socket address
typedef struct {
    uint16_t family, port;
    uint32_t ip;
} SOCK_ADDR;

// Socket storage
typedef struct {
    SOCK_ADDR addr;
    uint16_t localport, session;
    int state, conn_sock;
    uint32_t hif_data_addr;
    void (*handler)(uint8_t sock, int rxlen);
} SOCKET;

// Response message types
typedef union {
    uint8_t data[16];
    int val;
} RESP_MSG;

// P2P connection state tracking
static volatile bool p2p_connected = false;
static volatile bool p2p_dhcp_done = false;
static uint32_t p2p_my_ip = 0;
static uint32_t p2p_peer_ip = 0;

// External context from winc_lib.c
typedef struct {
    // Hardware pins
    struct {
        uint8_t sck, mosi, miso, cs, wake, reset, irq;
    } pins;

    // SPI buffers
    uint8_t txbuf[1600];
    uint8_t rxbuf[1600];
    uint8_t tx_zeros[1024];

    // Sockets
    SOCKET sockets[10];
    uint8_t databuf[1600];
    RESP_MSG resp_msg;

    // Config
    int verbose;
    bool use_crc;

    // Firmware info
    uint8_t fw_major, fw_minor, fw_patch;
    uint8_t mac[6];

    // Connection state
    struct {
        bool connected;
        bool dhcp_done;
        bool ap_mode;
    } connection_state;

    // Mesh state
    struct {
        uint8_t my_node_id;
        char my_name[16];
        bool enabled;
        int udp_socket;  // IMPORTANT: UDP socket for mesh

        // Routing table
        struct {
            uint8_t node_id;
            uint8_t next_hop;
            uint8_t hop_count;
            uint32_t last_seen;
            bool active;
        } routes[WINC_MESH_MAX_NODES];
        uint8_t route_count;

        uint16_t seq_num;
        uint32_t last_beacon;

        // Data callback
        void (*data_callback)(uint8_t, uint8_t*, uint16_t);
    } mesh;
} winc_ctx_t;

// External functions from winc_lib.c (they use global context, no fd param)
extern winc_ctx_t g_ctx;

// Declare internal functions from winc_lib.c that we need
bool hif_put(uint16_t gop, void *dp1, int dlen1, void *dp2, int dlen2, int oset);
bool put_sock_sendto(uint8_t sock, void *data, int len);
bool get_sock_data(uint8_t sock, void *data, int len);
int open_sock_server(int portnum, bool tcp, void (*handler)(uint8_t, int));

// Forward declarations
static void mesh_packet_handler(uint8_t sock, int rxlen);
static bool mesh_send_beacon(void);
static void mesh_handle_beacon(winc_mesh_beacon_t *beacon);
static bool mesh_route_packet(winc_mesh_hdr_t *hdr, uint8_t *data);
static int mesh_find_route(uint8_t dst_node);
static void mesh_update_route(uint8_t node_id, uint8_t next_hop, uint8_t hop_count);

// ===== P2P CONTROL FUNCTIONS =====

// Define P2P/WPS operations if not already defined
#ifndef GOP_WPS_REQ
#define GOP_WPS_REQ GIDOP(GID_WIFI, 73)
#endif

// Enable P2P mode on ATWINC1500
static bool p2p_enable(uint8_t channel) {
    P2P_ENABLE_CMD cmd;
    bool ok;

    if (g_ctx.verbose)
        printf("Enabling P2P mode on channel %u\n", channel);

    memset(&cmd, 0, sizeof(cmd));
    cmd.channel = channel;

    ok = hif_put(GOP_P2P_ENABLE, &cmd, sizeof(cmd), 0, 0, 0);

    if (ok && g_ctx.verbose)
        printf("P2P mode enabled\n");
    else if (!ok)
        printf("Failed to enable P2P mode\n");

    return ok;
}

// Disable P2P mode
static bool p2p_disable(void) {
    bool ok;

    if (g_ctx.verbose)
        printf("Disabling P2P mode\n");

    ok = hif_put(GOP_P2P_DISABLE, NULL, 0, 0, 0, 0);

    if (ok && g_ctx.verbose)
        printf("P2P mode disabled\n");

    return ok;
}

// Start WPS connection (for P2P pairing)
static bool p2p_start_wps_connection(void) {
    // WPS request structure
    typedef struct {
        uint8_t trigger_type;  // 4 = WPS_PBC
        uint8_t x[3];
        uint8_t pin[8];
    } WPS_REQ;

    WPS_REQ req;
    bool ok;

    printf("Initiating WPS Push Button Configuration...\n");

    memset(&req, 0, sizeof(req));
    req.trigger_type = 4;  // WPS_PBC

    ok = hif_put(GOP_WPS_REQ, &req, sizeof(req), 0, 0, 0);

    if (!ok) {
        printf("ERROR: WPS request failed\n");
    }

    return ok;
}

// ===== MESH INITIALIZATION =====
bool winc_mesh_init(uint8_t node_id, const char *node_name) {
    printf("\n========================================\n");
    printf("MESH INIT: Node %u (%s)\n", node_id, node_name);
    printf("========================================\n");

    // Store mesh config
    g_ctx.mesh.my_node_id = node_id;
    strncpy(g_ctx.mesh.my_name, node_name, sizeof(g_ctx.mesh.my_name) - 1);
    g_ctx.mesh.my_name[sizeof(g_ctx.mesh.my_name) - 1] = '\0';

    // Initialize routing table
    memset(g_ctx.mesh.routes, 0, sizeof(g_ctx.mesh.routes));
    g_ctx.mesh.route_count = 0;
    g_ctx.mesh.seq_num = 0;
    g_ctx.mesh.last_beacon = 0;

    // Enable verbose for debugging
    g_ctx.verbose = 1;

    bool network_ready = false;

    if (node_id == 1) {
        // Node 1: Start as AP
        printf("\n*** ROLE: ACCESS POINT (GROUP OWNER) ***\n");
        g_ctx.connection_state.ap_mode = true;

        network_ready = winc_start_ap("CAPSULE-MESH", "capsule123", WINC_P2P_CHANNEL);

        if (!network_ready) {
            printf("ERROR: Failed to start AP mode\n");
            return false;
        }

    } else {
        // Node 2+: Connect as client
        printf("\n*** ROLE: CLIENT (STATION) ***\n");

        network_ready = winc_connect_sta("CAPSULE-MESH", "capsule123");

        if (!network_ready) {
            printf("ERROR: Failed to connect to AP\n");
            return false;
        }
    }

    printf("\nWaiting for network to stabilize...\n");
    uint32_t stabilize_start = to_ms_since_boot(get_absolute_time());

    while ((to_ms_since_boot(get_absolute_time()) - stabilize_start) < 3000) {
        winc_poll();

        if (g_ctx.connection_state.connected && g_ctx.connection_state.dhcp_done) {
            printf("Network ready (connected=%d, dhcp=%d)\n",
                   g_ctx.connection_state.connected, g_ctx.connection_state.dhcp_done);
            break;
        }
        sleep_ms(100);
    }

    if (!g_ctx.connection_state.connected || !g_ctx.connection_state.dhcp_done) {
        printf("ERROR: Network not ready after stabilization period\n");
        printf("  connected=%d, dhcp_done=%d\n",
               g_ctx.connection_state.connected, g_ctx.connection_state.dhcp_done);
        return false;
    }

    printf("Creating UDP socket on port %d...\n", WINC_MESH_PORT);

    g_ctx.mesh.udp_socket = open_sock_server(WINC_MESH_PORT, false, mesh_packet_handler);

    if (g_ctx.mesh.udp_socket < 0) {
        printf("ERROR: Failed to create UDP socket\n");
        return false;
    }

    printf("UDP socket created: %d\n", g_ctx.mesh.udp_socket);

    // Wait for socket to bind
    printf("Waiting for socket to bind...\n");
    uint32_t wait_start = to_ms_since_boot(get_absolute_time());

    while ((to_ms_since_boot(get_absolute_time()) - wait_start) < 5000) {
        // Use winc_poll() to handle interrupts
        winc_poll();

        if (g_ctx.sockets[g_ctx.mesh.udp_socket].state == STATE_BOUND) {
            printf("Socket bound successfully!\n");
            break;
        }
        sleep_ms(100);
    }

    g_ctx.mesh.enabled = true;

    printf("\n========================================\n");
    printf("MESH INITIALIZATION COMPLETE!\n");
    printf("Role: %s\n", node_id == 1 ? "AP" : "Client");
    printf("Socket: %d (state=%d)\n",
           g_ctx.mesh.udp_socket,
           g_ctx.sockets[g_ctx.mesh.udp_socket].state);
    printf("========================================\n\n");

    return true;
}

// ===== MESH BEACON FUNCTIONS =====
static bool mesh_send_beacon(void) {
    winc_mesh_beacon_t beacon;
    uint8_t idx = 0;

    // Comprehensive checks before sending
    if (!g_ctx.mesh.enabled) {
        printf("[BEACON] ERROR: Mesh not enabled!\n");
        return false;
    }

    if (g_ctx.mesh.udp_socket < 0) {
        printf("[BEACON] ERROR: Invalid socket (%d)\n", g_ctx.mesh.udp_socket);
        return false;
    }

    if (g_ctx.sockets[g_ctx.mesh.udp_socket].state != STATE_BOUND) {
        printf("[BEACON] ERROR: Socket not bound (state=%d)\n",
               g_ctx.sockets[g_ctx.mesh.udp_socket].state);
        return false;
    }

    memset(&beacon, 0, sizeof(beacon));

    // Build beacon header
    beacon.hdr.msg_type = MESH_MSG_BEACON;
    beacon.hdr.src_node = g_ctx.mesh.my_node_id;
    beacon.hdr.dst_node = 0xFF;  // Broadcast
    beacon.hdr.seq_num = g_ctx.mesh.seq_num++;
    beacon.hdr.payload_len = sizeof(beacon) - sizeof(beacon.hdr);

    // Build beacon data
    beacon.node_id = g_ctx.mesh.my_node_id;
    strncpy((char*)beacon.node_name, g_ctx.mesh.my_name, sizeof(beacon.node_name) - 1);

    // Add direct neighbors (1-hop nodes)
    for (int i = 0; i < g_ctx.mesh.route_count && idx < WINC_MESH_MAX_NODES; i++) {
        if (g_ctx.mesh.routes[i].active && g_ctx.mesh.routes[i].hop_count == 1) {
            beacon.neighbors[idx++] = g_ctx.mesh.routes[i].node_id;
        }
    }
    beacon.neighbor_count = idx;

    printf("[BEACON] Sending beacon from node %u (%u neighbors, socket=%d, size=%u)\n",
           g_ctx.mesh.my_node_id, beacon.neighbor_count, g_ctx.mesh.udp_socket, sizeof(beacon));

    bool result = put_sock_sendto(g_ctx.mesh.udp_socket, &beacon, sizeof(beacon));
    if (!result) {
        printf("[BEACON] ERROR: Failed to send beacon!\n");
    }
    return result;
}

// Handle received beacon
static void mesh_handle_beacon(winc_mesh_beacon_t *beacon) {
    uint32_t now = to_ms_since_boot(get_absolute_time());

    printf("[BEACON] Received beacon from node %u (%s), %u neighbors\n",
           beacon->node_id, beacon->node_name, beacon->neighbor_count);

    if (g_ctx.verbose > 1)
        printf("Beacon from node %u (%s), %u neighbors\n",
               beacon->node_id, beacon->node_name, beacon->neighbor_count);

    // Update direct route to beacon sender (1 hop)
    mesh_update_route(beacon->node_id, beacon->node_id, 1);

    // Update indirect routes through beacon sender
    for (int i = 0; i < beacon->neighbor_count; i++) {
        uint8_t neighbor_id = beacon->neighbors[i];
        
        // Skip ourselves
        if (neighbor_id == g_ctx.mesh.my_node_id)
            continue;

        // Update or add 2-hop route through beacon sender
        mesh_update_route(neighbor_id, beacon->node_id, 2);
    }
}

// ===== MESH DATA FUNCTIONS =====
bool winc_mesh_send(uint8_t dst_node, uint8_t *data, uint16_t len) {
    struct {
        winc_mesh_hdr_t hdr;
        uint8_t payload[];
    } __attribute__((packed)) *pkt;

    int next_hop;
    bool result = false;

    if (!g_ctx.mesh.enabled) {
        printf("ERROR: Mesh not enabled (P2P mode or UDP socket failed during init)\n");
        return false;
    }

    if (g_ctx.mesh.udp_socket < 0) {
        printf("ERROR: UDP socket invalid (socket=%d)\n", g_ctx.mesh.udp_socket);
        return false;
    }

    // Find route to destination
    next_hop = mesh_find_route(dst_node);
    if (next_hop < 0) {
        printf("ERROR: No route to node %u (known nodes: %u)\n", dst_node, g_ctx.mesh.route_count);
        return false;
    }

    // Allocate packet buffer
    pkt = malloc(sizeof(winc_mesh_hdr_t) + len);
    if (!pkt) {
        printf("Failed to allocate packet buffer\n");
        return false;
    }

    // Build packet header
    pkt->hdr.msg_type = MESH_MSG_DATA;
    pkt->hdr.src_node = g_ctx.mesh.my_node_id;
    pkt->hdr.dst_node = dst_node;
    pkt->hdr.hop_count = 0;
    pkt->hdr.seq_num = g_ctx.mesh.seq_num++;
    pkt->hdr.payload_len = len;

    // Copy payload
    memcpy(pkt->payload, data, len);

    if (g_ctx.verbose)
        printf("Sending %u bytes to node %u via hop %d\n", len, dst_node, next_hop);

    result = put_sock_sendto(g_ctx.mesh.udp_socket, pkt, sizeof(winc_mesh_hdr_t) + len);

    if (!result) {
        printf("ERROR: put_sock_sendto failed (socket=%d, len=%u)\n",
               g_ctx.mesh.udp_socket, sizeof(winc_mesh_hdr_t) + len);
    }

    free(pkt);
    return result;
}

// Route a packet through the mesh
static bool mesh_route_packet(winc_mesh_hdr_t *hdr, uint8_t *data) {
    int next_hop;

    // Check if packet is for us
    if (hdr->dst_node == g_ctx.mesh.my_node_id) {
        // Deliver to application
        if (g_ctx.mesh.data_callback) {
            g_ctx.mesh.data_callback(hdr->src_node, data, hdr->payload_len);
        }
        return true;
    }

    // Check hop count
    if (hdr->hop_count >= MESH_MSG_ROUTE_RESP) {  // Max 4 hops
        if (g_ctx.verbose)
            printf("Packet exceeded max hops, dropping\n");
        return false;
    }

    // Find next hop
    next_hop = mesh_find_route(hdr->dst_node);
    if (next_hop < 0) {
        if (g_ctx.verbose)
            printf("No route to node %u, dropping packet\n", hdr->dst_node);
        return false;
    }

    // Increment hop count and forward
    hdr->hop_count++;

    if (g_ctx.verbose > 1)
        printf("Forwarding packet to node %u via hop %d\n", hdr->dst_node, next_hop);

    // Forward packet
    return put_sock_sendto(g_ctx.mesh.udp_socket, hdr, 
                          sizeof(winc_mesh_hdr_t) + hdr->payload_len);
}

// ===== ROUTING TABLE FUNCTIONS =====
static void mesh_update_route(uint8_t node_id, uint8_t next_hop, uint8_t hop_count) {
    uint32_t now = to_ms_since_boot(get_absolute_time());
    int existing = -1;
    int free_slot = -1;

    // Find existing entry or free slot
    for (int i = 0; i < WINC_MESH_MAX_NODES; i++) {
        if (g_ctx.mesh.routes[i].active && g_ctx.mesh.routes[i].node_id == node_id) {
            existing = i;
            break;
        }
        if (!g_ctx.mesh.routes[i].active && free_slot < 0) {
            free_slot = i;
        }
    }

    // Update existing or add new
    if (existing >= 0) {
        // Update if better route or refresh same route
        if (hop_count <= g_ctx.mesh.routes[existing].hop_count) {
            g_ctx.mesh.routes[existing].next_hop = next_hop;
            g_ctx.mesh.routes[existing].hop_count = hop_count;
            g_ctx.mesh.routes[existing].last_seen = now;
        }
    } else if (free_slot >= 0) {
        // Add new route
        g_ctx.mesh.routes[free_slot].node_id = node_id;
        g_ctx.mesh.routes[free_slot].next_hop = next_hop;
        g_ctx.mesh.routes[free_slot].hop_count = hop_count;
        g_ctx.mesh.routes[free_slot].last_seen = now;
        g_ctx.mesh.routes[free_slot].active = true;
        
        if (free_slot >= g_ctx.mesh.route_count) {
            g_ctx.mesh.route_count = free_slot + 1;
        }

        if (g_ctx.verbose)
            printf("New route: Node %u via %u (%u hops)\n", node_id, next_hop, hop_count);
    }
}

// Find best route to destination
static int mesh_find_route(uint8_t dst_node) {
    int best = -1;
    uint8_t min_hops = 255;

    for (int i = 0; i < g_ctx.mesh.route_count; i++) {
        if (g_ctx.mesh.routes[i].active &&
            g_ctx.mesh.routes[i].node_id == dst_node &&
            g_ctx.mesh.routes[i].hop_count < min_hops) {
            best = i;
            min_hops = g_ctx.mesh.routes[i].hop_count;
        }
    }

    return (best >= 0) ? g_ctx.mesh.routes[best].next_hop : -1;
}

// ===== PACKET HANDLER =====

// Handle incoming mesh packets (called by socket layer)
static void mesh_packet_handler(uint8_t sock, int rxlen) {
    uint8_t buf[1600];
    winc_mesh_hdr_t *hdr;

    printf("[RX] Packet received on socket %u, length=%d\n", sock, rxlen);

    if (rxlen <= 0) {
        if (rxlen < 0)
            printf("[RX] ERROR: Socket error %d\n", rxlen);
        return;
    }

    // Get packet data
    if (!get_sock_data(sock, buf, rxlen)) {
        printf("[RX] ERROR: Failed to get mesh packet data\n");
        return;
    }

    hdr = (winc_mesh_hdr_t*)buf;

    printf("[RX] Mesh packet: type=%u, src=%u, dst=%u, hops=%u, len=%u\n",
           hdr->msg_type, hdr->src_node, hdr->dst_node,
           hdr->hop_count, hdr->payload_len);

    // Process based on message type
    switch (hdr->msg_type) {
        case MESH_MSG_BEACON:
            printf("[RX] Processing BEACON from node %u\n", hdr->src_node);
            mesh_handle_beacon((winc_mesh_beacon_t*)buf);
            break;

        case MESH_MSG_DATA:
            if (hdr->dst_node == g_ctx.mesh.my_node_id || hdr->dst_node == 0xFF) {
                // Packet is for us
                if (g_ctx.mesh.data_callback) {
                    g_ctx.mesh.data_callback(hdr->src_node,
                                           buf + sizeof(winc_mesh_hdr_t),
                                           hdr->payload_len);
                }
            } else {
                // Route to next hop
                mesh_route_packet(hdr, buf + sizeof(winc_mesh_hdr_t));
            }
            break;

        default:
            if (g_ctx.verbose)
                printf("Unknown mesh message type: %u\n", hdr->msg_type);
            break;
    }
}

// ===== MESH PROCESSING =====

// Process mesh events (called from winc_poll)
void winc_mesh_process(void) {
    static bool first_call = true;
    uint32_t now = to_ms_since_boot(get_absolute_time());

    if (first_call) {
        printf("[MESH] winc_mesh_process called for first time (enabled=%d)\n", g_ctx.mesh.enabled);
        first_call = false;
    }

    if (!g_ctx.mesh.enabled)
        return;

    // Send periodic beacons
    if (now - g_ctx.mesh.last_beacon > WINC_MESH_BEACON_INTERVAL_MS) {
        printf("[MESH] Time to send beacon (last=%lu, now=%lu, interval=%d)\n",
               g_ctx.mesh.last_beacon, now, WINC_MESH_BEACON_INTERVAL_MS);
        mesh_send_beacon();
        g_ctx.mesh.last_beacon = now;
    }

    // Timeout old routes
    for (int i = 0; i < g_ctx.mesh.route_count; i++) {
        if (g_ctx.mesh.routes[i].active &&
            (now - g_ctx.mesh.routes[i].last_seen > WINC_MESH_ROUTE_TIMEOUT_MS)) {
            if (g_ctx.verbose)
                printf("Route to node %u timed out\n", g_ctx.mesh.routes[i].node_id);
            g_ctx.mesh.routes[i].active = false;
        }
    }
}

// ===== UTILITY FUNCTIONS =====

// Print routing table (implementation already in winc_lib.c)
void mesh_print_routing_table(void) {
    printf("\n=== Mesh Routing Table ===\n");
    printf("Local Node: %u (%s)\n", g_ctx.mesh.my_node_id, g_ctx.mesh.my_name);
    printf("Active Routes: %u\n", g_ctx.mesh.route_count);
    
    if (g_ctx.mesh.route_count > 0) {
        printf("\nNode  Hops  Next-Hop  Last-Seen  Status\n");
        printf("----  ----  --------  ---------  ------\n");
        
        for (int i = 0; i < g_ctx.mesh.route_count; i++) {
            if (g_ctx.mesh.routes[i].active) {
                uint32_t age = (to_ms_since_boot(get_absolute_time()) - 
                               g_ctx.mesh.routes[i].last_seen) / 1000;
                printf("%4u  %4u  %8u  %7us  Active\n",
                       g_ctx.mesh.routes[i].node_id,
                       g_ctx.mesh.routes[i].hop_count,
                       g_ctx.mesh.routes[i].next_hop,
                       age);
            }
        }
    } else {
        printf("No routes discovered yet\n");
    }
    printf("========================\n\n");
}