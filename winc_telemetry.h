// Hybrid TCP/UDP telemetry system
#ifndef WINC_TELEMETRY_H
#define WINC_TELEMETRY_H

#include <stdint.h>
#include <stdbool.h>
#include "pico/stdlib.h"
#include "pico/multicore.h"
#include "pico/mutex.h"
#include "hardware/crc.h"

typdef enum {
    TELEM_CRITICAL = 0,
    TELEM_HIGH = 1,
    TELEM_NORMAL = 2,
    TELEM_LOW = 3
} telem_priority_t;

typedef enum {
    PKT_BEACON = 0x01,
    PKT_TELEMETRY = 0x02,
    PKT_COMMAND = 0x03,
    PKT_ACK = 0x04,
    PKT_FILE = 0x05,
    PKT_EMERGENCY = 0x06
} packet_type_t;

#define CRC32_POLYNOMIAL 0xEDB88320
#define MAX_RETRIES 3
#define REDUNDUNCY_FACTOR 3     // Number of times to send critical packets

typdef struct {
    uint32_t crc32;
    uint16_t sequence;
    uint8_t redunduncy_id;
    uint8_t retry_count;
} packet_integrity_t;

typedef struct {    //Header (16 bytes)
    uint8_t type;
    uint8_t priority;
    uint16_t length;
    uint32_t timestamp;
    uint8_t src_node;
    uint8_t dst_node;
    uint16_t dst node;
    uint16_t flags;
    packet_integrity_t integrity;

    //Payload (up to 1024 bytes?)
    uint8_t payload[1024];
} telemetry_packet_t;

typedef struct {
    int sock;
    uint8_t remote_node;
    uint32_t remote_ip;
    uint16_t remote_port;
    bool connected;
    uint32_t last_activity;
    uint32_t bytes_sent;
    uint32_t bytes_received;
    uint8_t retry_count;
} tcp_connection_t;

typedef struct {
    mutex_t socket_mutex;
    mutex_t queue_mutex;
    mutex_t stats_mutex;
    volatile bool core1_ready;
    volatile bool shutdown;
} thread_sync_t;

typedef struct {
    uint32_t packets_sent;
    uint32_t packets_received;
    uint32_t crc_errors;
    uint32_t retransmissions;
    uint32_t radiation_events;
    uint32_t tcp_connections;
    uint32_t udp_connections;
    float packet_loss_rate;
} telemetry_stats_t;

// Global context
typedef struct {
    //Network sockets
    int udp_sock;
    int tcp_listen_sock;
    tcp_connection_t tcp_connections[4];    //up to 4 simultaneous TCP connections
    
    thread_sync_t sync; 

    // Packet queues
    telemetry_packet_t *tx_queue[32];
    telemetry_packet_t *rx_queue[32];
    uint8_t tx_head, tx_tail;
    uint8_t rx_head, rx_tail;

    telemetry_stats_t stats;

    // Configuration
    uint8_t node_id;
    uint8_t iridium_gateway; //True if this node has Iridium
    uint32_t beacon_interval; // Milliseconds
} telemetry_ctx_t;

// ====== FUNCTION PROTOTYPES ======
//Initialization
bool telemetry_init(uint8_t node_id, bool is_gateway);
void telemetry_shutdown(void);

//TCP functions
int tcp_server_init(uint16_t port);
int tcp_client_connect(uint32_t server_ip, uint16_t port);
bool tcp_send_critical(uint8_t dst_node, void *data, uint16_t len);
void tcp_connection_handler(uint8_t sock, int rxlen);

//UDP functions
bool udp_broadcast_beacon(void);
bool udp_send_telemetry(uint8_t dst_node, void *data, uint16_t len, telem_priority_t priority);

// CRC and Radiation Tolerance
uint32_t calculate_crc32(const uint8_t *data, size_t length);
bool verify_packet_integrity(telemetry_packet_t *packet);
bool send_with_redundancy(telemetry_packet_t *packet);
telemetry_packet_t* recover_from_redundancy(telemetry_packet_t *packets, int count);

// Thread Functions (Core 1)
void core1_network_handler(void);
void process_ap_mode(void);
void process_p2p_mode(void);

// Queue Management
bool enqueue_packet(telemetry_packet_t *packet, bool is_tx);
telemetry_packet_t* dequeue_packet(bool is_tx);

#endif // WINC_TELEMETRY_H