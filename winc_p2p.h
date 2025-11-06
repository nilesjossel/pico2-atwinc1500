// ATWINC1500/1510 WiFi module P2P/mesh definitions for the Pico 2W
//
// P2P (Wi-Fi Direct) and mesh networking support
// Based on ATWINC1500 P2P mode application note
//
// Created by Niles Roxas (2025)


#ifndef WINC_P2P_H
#define WINC_P2P_H

#include <stdint.h>
#include <stdbool.h>

// P2P Group IDs and Operations
#define GOP_P2P_ENABLE      GIDOP(GID_WIFI, 85)  // Enable P2P mode
#define GOP_P2P_DISABLE     GIDOP(GID_WIFI, 86)  // Disable P2P mode
#define GOP_WPS_REQ         GIDOP(GID_WIFI, 73)  // WPS request (used in P2P)
#define GOP_WPS_RESP        GIDOP(GID_WIFI, 74)  // WPS response
#define GOP_P2P_CONN_REQ    GIDOP(GID_WIFI, 87)  // P2P connection request
#define GOP_P2P_CONN_RESP   GIDOP(GID_WIFI, 88)  // P2P connection response

// P2P Mode types
#define P2P_MODE_IDLE       0
#define P2P_MODE_LISTEN     1
#define P2P_MODE_SEARCH     2

// P2P Channel options
#define P2P_CHAN_1          1
#define P2P_CHAN_6          6
#define P2P_CHAN_11         11
#define P2P_ANY_CHAN        255

// P2P Listen channel and period (in ms)
#define P2P_LISTEN_CHAN     P2P_CHAN_1
#define P2P_LISTEN_PERIOD   100

// WPS trigger types for P2P
#define WPS_PBC             4   // Push Button Configuration
#define WPS_PIN             0   // PIN method

// Mesh network configuration
#define MESH_MAX_NODES      8
#define MESH_BEACON_INTERVAL 5000  // Beacon interval in ms
#define MESH_ROUTE_TIMEOUT  30000  // Route timeout in ms
#define MESH_MAX_HOPS       4      // Maximum hops in mesh

// Mesh message types
#define MESH_MSG_BEACON     0x01
#define MESH_MSG_DATA       0x02
#define MESH_MSG_ROUTE_REQ  0x03
#define MESH_MSG_ROUTE_RESP 0x04
#define MESH_MSG_ACK        0x05

// P2P Enable command structure
typedef struct {
    uint8_t channel;
    uint8_t x[3];
} P2P_ENABLE_CMD;

// P2P Connection request structure
typedef struct {
    uint8_t device_name[32];
    uint8_t listen_channel;
    uint8_t operating_channel;
    uint8_t x[2];
} P2P_CONN_REQ;

// WPS request structure for P2P
typedef struct {
    uint8_t trigger_type;  // WPS_PBC or WPS_PIN
    uint8_t x[3];
    uint8_t pin[8];        // PIN if using PIN method
} WPS_REQ;

// P2P peer information
typedef struct {
    uint8_t mac_addr[6];
    uint8_t device_name[32];
    uint8_t channel;
    int8_t rssi;
    uint32_t last_seen;
} P2P_PEER;

// Mesh node information
typedef struct {
    uint8_t node_id;
    uint8_t mac_addr[6];
    uint8_t hop_count;
    uint8_t next_hop;      // Next hop node_id to reach this node
    uint32_t last_update;
    bool is_active;
} MESH_NODE;

// Mesh routing table
typedef struct {
    MESH_NODE nodes[MESH_MAX_NODES];
    uint8_t node_count;
    uint8_t local_node_id;
} MESH_ROUTING_TABLE;

// Mesh packet header
typedef struct {
    uint8_t msg_type;
    uint8_t src_node;
    uint8_t dst_node;
    uint8_t hop_count;
    uint16_t seq_num;
    uint16_t payload_len;
} MESH_PKT_HDR;

// Mesh beacon packet
typedef struct {
    MESH_PKT_HDR hdr;
    uint8_t node_id;
    uint8_t node_name[16];
    uint8_t neighbors[MESH_MAX_NODES];
    uint8_t neighbor_count;
} MESH_BEACON;

// Function declarations

// P2P Mode Functions
bool p2p_enable(int fd, uint8_t channel);
bool p2p_disable(int fd);
bool p2p_start_listen(int fd, uint8_t channel);
bool p2p_start_search(int fd);
bool p2p_connect(int fd, char *device_name, uint8_t channel);
bool p2p_connect_wps_pbc(int fd);
bool p2p_connect_wps_pin(int fd, uint8_t *pin);
void p2p_peer_found_handler(P2P_PEER *peer);

// Mesh Network Functions
bool mesh_init(int fd, uint8_t node_id, char *node_name);
bool mesh_enable(int fd);
bool mesh_disable(int fd);
bool mesh_send_beacon(int fd);
bool mesh_send_data(int fd, uint8_t dst_node, uint8_t *data, uint16_t len);
bool mesh_route_packet(int fd, MESH_PKT_HDR *pkt, uint8_t *data);
void mesh_update_routing_table(MESH_BEACON *beacon);
int mesh_find_route(uint8_t dst_node);
void mesh_beacon_handler(int fd);
void mesh_data_handler(int fd, uint8_t *data, uint16_t len);

// Utility functions
void mesh_print_routing_table(void);
bool is_p2p_enabled(void);
bool is_mesh_enabled(void);

#endif // WINC_P2P_H

// EOF
