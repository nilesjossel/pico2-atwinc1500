// winc_telemetry.c - Implementation of Hybrid TCP/UDP Telemetry System
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "pico/stdlib.h"
#include "pico/multicore.h"
#include "pico/mutex.h"
#include "hardware/watchdog.h"
#include "winc_telemetry.h"
#include "winc_lib.h"
#include "winc_mesh.h"

// Global telemetry context
static telemetry_ctx_t telem_ctx;
extern winc_ctx_t g_ctx;  // From winc_lib.c

// ============= CRC32 IMPLEMENTATION =============
// ROM lookup table for CRC32 calculation
static const uint32_t crc32_table[256] = {
    // Table entries for bytes 0x00-0x0F
    0x00000000, 0x77073096, 0xEE0E612C, 0x990951BA,
    0x076DC419, 0x706AF48F, 0xE963A535, 0x9E6495A3,
    0x0EDB8832, 0x79DCB8A4, 0xE0D5E91E, 0x97D2D988,
    0x09B64C2B, 0x7EB17CBD, 0xE7B82D07, 0x90BF1D91,
    
    // Table entries for bytes 0x10-0x1F
    0x1DB71064, 0x6AB020F2, 0xF3B97148, 0x84BE41DE,
    0x1ADAD47D, 0x6DDDE4EB, 0xF4D4B551, 0x83D385C7,
    0x136C9856, 0x646BA8C0, 0xFD62F97A, 0x8A65C9EC,
    0x14015C4F, 0x63066CD9, 0xFA0F3D63, 0x8D080DF5,
    
    // Table entries for bytes 0x20-0x2F
    0x3B6E20C8, 0x4C69105E, 0xD56041E4, 0xA2677172,
    0x3C03E4D1, 0x4B04D447, 0xD20D85FD, 0xA50AB56B,
    0x35B5A8FA, 0x42B2986C, 0xDBBBC9D6, 0xACBCF940,
    0x32D86CE3, 0x45DF5C75, 0xDCD60DCF, 0xABD13D59,
    
    // Table entries for bytes 0x30-0x3F
    0x26D930AC, 0x51DE003A, 0xC8D75180, 0xBFD06116,
    0x21B4F4B5, 0x56B3C423, 0xCFBA9599, 0xB8BDA50F,
    0x2802B89E, 0x5F058808, 0xC60CD9B2, 0xB10BE924,
    0x2F6F7C87, 0x58684C11, 0xC1611DAB, 0xB6662D3D,
    
    // Table entries for bytes 0x40-0x4F
    0x76DC4190, 0x01DB7106, 0x98D220BC, 0xEFD5102A,
    0x71B18589, 0x06B6B51F, 0x9FBFE4A5, 0xE8B8D433,
    0x7807C9A2, 0x0F00F934, 0x9609A88E, 0xE10E9818,
    0x7F6A0DBB, 0x086D3D2D, 0x91646C97, 0xE6635C01,
    
    // Table entries for bytes 0x50-0x5F
    0x6B6B51F4, 0x1C6C6162, 0x856530D8, 0xF262004E,
    0x6C0695ED, 0x1B01A57B, 0x8208F4C1, 0xF50FC457,
    0x65B0D9C6, 0x12B7E950, 0x8BBEB8EA, 0xFCB9887C,
    0x62DD1DDF, 0x15DA2D49, 0x8CD37CF3, 0xFBD44C65,
    
    // Table entries for bytes 0x60-0x6F
    0x4DB26158, 0x3AB551CE, 0xA3BC0074, 0xD4BB30E2,
    0x4ADFA541, 0x3DD895D7, 0xA4D1C46D, 0xD3D6F4FB,
    0x4369E96A, 0x346ED9FC, 0xAD678846, 0xDA60B8D0,
    0x44042D73, 0x33031DE5, 0xAA0A4C5F, 0xDD0D7CC9,
    
    // Table entries for bytes 0x70-0x7F
    0x5005713C, 0x270241AA, 0xBE0B1010, 0xC90C2086,
    0x5768B525, 0x206F85B3, 0xB966D409, 0xCE61E49F,
    0x5EDEF90E, 0x29D9C998, 0xB0D09822, 0xC7D7A8B4,
    0x59B33D17, 0x2EB40D81, 0xB7BD5C3B, 0xC0BA6CAD,
    
    // Table entries for bytes 0x80-0x8F
    0xEDB88320, 0x9ABFB3B6, 0x03B6E20C, 0x74B1D29A,
    0xEAD54739, 0x9DD277AF, 0x04DB2615, 0x73DC1683,
    0xE3630B12, 0x94643B84, 0x0D6D6A3E, 0x7A6A5AA8,
    0xE40ECF0B, 0x9309FF9D, 0x0A00AE27, 0x7D079EB1,
    
    // Table entries for bytes 0x90-0x9F
    0xF00F9344, 0x8708A3D2, 0x1E01F268, 0x6906C2FE,
    0xF762575D, 0x806567CB, 0x196C3671, 0x6E6B06E7,
    0xFED41B76, 0x89D32BE0, 0x10DA7A5A, 0x67DD4ACC,
    0xF9B9DF6F, 0x8EBEEFF9, 0x17B7BE43, 0x60B08ED5,
    
    // Table entries for bytes 0xA0-0xAF
    0xD6D6A3E8, 0xA1D1937E, 0x38D8C2C4, 0x4FDFF252,
    0xD1BB67F1, 0xA6BC5767, 0x3FB506DD, 0x48B2364B,
    0xD80D2BDA, 0xAF0A1B4C, 0x36034AF6, 0x41047A60,
    0xDF60EFC3, 0xA867DF55, 0x316E8EEF, 0x4669BE79,
    
    // Table entries for bytes 0xB0-0xBF
    0xCB61B38C, 0xBC66831A, 0x256FD2A0, 0x5268E236,
    0xCC0C7795, 0xBB0B4703, 0x220216B9, 0x5505262F,
    0xC5BA3BBE, 0xB2BD0B28, 0x2BB45A92, 0x5CB36A04,
    0xC2D7FFA7, 0xB5D0CF31, 0x2CD99E8B, 0x5BDEAE1D,
    
    // Table entries for bytes 0xC0-0xCF
    0x9B64C2B0, 0xEC63F226, 0x756AA39C, 0x026D930A,
    0x9C0906A9, 0xEB0E363F, 0x72076785, 0x05005713,
    0x95BF4A82, 0xE2B87A14, 0x7BB12BAE, 0x0CB61B38,
    0x92D28E9B, 0xE5D5BE0D, 0x7CDCEFB7, 0x0BDBDF21,
    
    // Table entries for bytes 0xD0-0xDF
    0x86D3D2D4, 0xF1D4E242, 0x68DDB3F8, 0x1FDA836E,
    0x81BE16CD, 0xF6B9265B, 0x6FB077E1, 0x18B74777,
    0x88085AE6, 0xFF0F6A70, 0x66063BCA, 0x11010B5C,
    0x8F659EFF, 0xF862AE69, 0x616BFFD3, 0x166CCF45,
    
    // Table entries for bytes 0xE0-0xEF
    0xA00AE278, 0xD70DD2EE, 0x4E048354, 0x3903B3C2,
    0xA7672661, 0xD06016F7, 0x4969474D, 0x3E6E77DB,
    0xAED16A4A, 0xD9D65ADC, 0x40DF0B66, 0x37D83BF0,
    0xA9BCAE53, 0xDEBB9EC5, 0x47B2CF7F, 0x30B5FFE9,
    
    // Table entries for bytes 0xF0-0xFF
    0xBDBDF21C, 0xCABAC28A, 0x53B39330, 0x24B4A3A6,
    0xBAD03605, 0xCDD70693, 0x54DE5729, 0x23D967BF,
    0xB3667A2E, 0xC4614AB8, 0x5D681B02, 0x2A6F2B94,
    0xB40BBE37, 0xC30C8EA1, 0x5A05DF1B, 0x2D02EF8D
};

uint32_t calculate_crc32(const uint8_t *data, size_t length) {
    uint32_t crc = 0xFFFFFFFF;
    
    for (size_t i = 0; i < length; i++) {
        uint8_t byte = data[i];
        uint32_t lookup = (crc ^ byte) & 0xFF;
        crc = (crc >> 8) ^ crc32_table[lookup];
    }
    
    return crc ^ 0xFFFFFFFF;
}

// Alternative: Use Pico hardware CRC if available
uint32_t calculate_crc32_hw(const uint8_t *data, size_t length) {
    hw_set_bits(&dma_hw->sniff_ctrl, DMA_SNIFF_CTRL_OUT_REV_BITS);
    
    uint32_t crc = 0xFFFFFFFF;
    for (size_t i = 0; i < length; i++) {
        crc = __builtin_arm_crc32b(crc, data[i]);
    }
    return ~crc;
}

// ============= PACKET INTEGRITY & RADIATION TOLERANCE =============
bool verify_packet_integrity(telemetry_packet_t *packet) {
    // Calculate CRC excluding the CRC field itself
    uint32_t calc_crc = calculate_crc32((uint8_t*)packet + 4, 
                                         sizeof(telemetry_packet_t) - 4);
    
    if (calc_crc != packet->integrity.crc32) {
        telem_ctx.stats.crc_errors++;
        printf("[INTEGRITY] CRC mismatch: expected 0x%08X, got 0x%08X\n", 
               packet->integrity.crc32, calc_crc);
        return false;
    }
    
    return true;
}

bool send_with_redundancy(telemetry_packet_t *packet) {
    bool success = false;
    
    // For critical data, send multiple copies with different redundancy IDs
    int copies = (packet->priority == TELEM_CRITICAL) ? REDUNDANCY_FACTOR : 1;
    
    for (int i = 0; i < copies; i++) {
        packet->integrity.redundancy_id = i;
        packet->integrity.crc32 = calculate_crc32((uint8_t*)packet + 4, 
                                                  sizeof(telemetry_packet_t) - 4);
        
        // Send via TCP for critical, UDP for others
        if (packet->priority <= TELEM_HIGH) {
            success = tcp_send_critical(packet->dst_node, packet, 
                                      sizeof(telemetry_packet_t));
        } else {
            success = udp_send_telemetry(packet->dst_node, packet, 
                                        sizeof(telemetry_packet_t), 
                                        packet->priority);
        }
        
        if (!success && packet->integrity.retry_count < MAX_RETRIES) {
            packet->integrity.retry_count++;
            telem_ctx.stats.retransmissions++;
            sleep_ms(100 * (i + 1));  // Exponential backoff
        }
    }
    
    return success;
}

telemetry_packet_t* recover_from_redundancy(telemetry_packet_t *packets, int count) {
    // Vote on correct packet from redundant copies
    if (count == 1) {
        return verify_packet_integrity(&packets[0]) ? &packets[0] : NULL;
    }
    
    // For multiple copies, use majority voting
    int valid_count = 0;
    int best_index = -1;
    
    for (int i = 0; i < count; i++) {
        if (verify_packet_integrity(&packets[i])) {
            valid_count++;
            if (best_index == -1) best_index = i;
            
            // Compare with other valid packets
            for (int j = i + 1; j < count; j++) {
                if (verify_packet_integrity(&packets[j])) {
                    // Check if payloads match
                    if (memcmp(packets[i].payload, packets[j].payload, 
                              packets[i].length) == 0) {
                        // Matching packets found
                        return &packets[i];
                    }
                }
            }
        }
    }
    
    // Return best valid packet if found
    return (best_index >= 0) ? &packets[best_index] : NULL;
}

// ============= TCP SOCKET IMPLEMENTATION =============
int tcp_server_init(uint16_t port) {
    mutex_enter_blocking(&telem_ctx.sync.socket_mutex);
    
    int sock = open_sock_server(port, true, tcp_connection_handler);
    if (sock < 0) {
        printf("[TCP] Failed to create server socket on port %d\n", port);
        mutex_exit(&telem_ctx.sync.socket_mutex);
        return -1;
    }
    
    telem_ctx.tcp_listen_sock = sock;
    printf("[TCP] Server listening on port %d (socket %d)\n", port, sock);
    
    mutex_exit(&telem_ctx.sync.socket_mutex);
    return sock;
}

int tcp_client_connect(uint32_t server_ip, uint16_t port) {
    mutex_enter_blocking(&telem_ctx.sync.socket_mutex);
    
    // Find free connection slot
    int conn_idx = -1;
    for (int i = 0; i < 4; i++) {
        if (!telem_ctx.tcp_connections[i].connected) {
            conn_idx = i;
            break;
        }
    }
    
    if (conn_idx < 0) {
        printf("[TCP] No free connection slots\n");
        mutex_exit(&telem_ctx.sync.socket_mutex);
        return -1;
    }
    
    // Allocate TCP socket (0-6 are TCP)
    int sock = -1;
    for (int i = MIN_TCP_SOCK; i < MAX_TCP_SOCK; i++) {
        if (g_ctx.sockets[i].state == STATE_CLOSED) {
            sock = i;
            break;
        }
    }
    
    if (sock < 0) {
        printf("[TCP] No free TCP sockets\n");
        mutex_exit(&telem_ctx.sync.socket_mutex);
        return -1;
    }
    
    // Initialize connection structure
    tcp_connection_t *conn = &telem_ctx.tcp_connections[conn_idx];
    conn->sock = sock;
    conn->remote_ip = server_ip;
    conn->remote_port = port;
    conn->retry_count = 0;
    
    // Set up socket
    SOCKET *sp = &g_ctx.sockets[sock];
    sp->addr.family = IP_FAMILY;
    sp->addr.port = swap16(port);
    sp->addr.ip = server_ip;
    sp->localport = port + 1000 + conn_idx;  // Use different local port
    sp->session = rand() & 0xFFFF;
    sp->state = STATE_CONNECTING;
    sp->handler = tcp_connection_handler;
    
    // Send TCP connect command (needs implementation in winc_lib)
    // For now, we'll use the bind/listen model
    printf("[TCP] Initiating connection to %u.%u.%u.%u:%d\n",
           (server_ip >> 0) & 0xFF, (server_ip >> 8) & 0xFF,
           (server_ip >> 16) & 0xFF, (server_ip >> 24) & 0xFF, port);
    
    // Note: Full TCP client connect requires GOP_CONNECT implementation
    // This is a placeholder for the connection logic
    
    mutex_exit(&telem_ctx.sync.socket_mutex);
    return sock;
}

void tcp_connection_handler(uint8_t sock, int rxlen) {
    telemetry_packet_t packet;
    
    if (rxlen <= 0) {
        // Connection closed or error
        printf("[TCP] Socket %d closed/error (rxlen=%d)\n", sock, rxlen);
        
        // Find and clear connection
        for (int i = 0; i < 4; i++) {
            if (telem_ctx.tcp_connections[i].sock == sock) {
                telem_ctx.tcp_connections[i].connected = false;
                break;
            }
        }
        
        sock_state(sock, STATE_CLOSED);
        return;
    }
    
    // Receive data
    if (get_sock_data(sock, &packet, rxlen)) {
        printf("[TCP] Received %d bytes on socket %d\n", rxlen, sock);
        
        // Verify packet integrity
        if (verify_packet_integrity(&packet)) {
            // Process based on packet type
            switch (packet.type) {
                case PKT_EMERGENCY:
                    printf("[EMERGENCY] From node %d: ", packet.src_node);
                    // Handle emergency telemetry
                    break;
                    
                case PKT_COMMAND:
                    printf("[COMMAND] From node %d\n", packet.src_node);
                    // Process command and send ACK
                    telemetry_packet_t ack;
                    ack.type = PKT_ACK;
                    ack.src_node = telem_ctx.my_node_id;
                    ack.dst_node = packet.src_node;
                    tcp_send_critical(packet.src_node, &ack, sizeof(ack));
                    break;
                    
                case PKT_TELEMETRY:
                    // Queue for processing
                    enqueue_packet(&packet, false);
                    break;
                    
                default:
                    printf("[TCP] Unknown packet type: 0x%02X\n", packet.type);
            }
            
            // Update connection stats
            for (int i = 0; i < 4; i++) {
                if (telem_ctx.tcp_connections[i].sock == sock) {
                    telem_ctx.tcp_connections[i].bytes_received += rxlen;
                    telem_ctx.tcp_connections[i].last_activity = 
                        to_ms_since_boot(get_absolute_time());
                    break;
                }
            }
        } else {
            printf("[TCP] Packet integrity check failed\n");
        }
        
        // Continue receiving
        put_sock_recv(sock);
    }
}

bool tcp_send_critical(uint8_t dst_node, void *data, uint16_t len) {
    // Find connection to destination node
    tcp_connection_t *conn = NULL;
    for (int i = 0; i < 4; i++) {
        if (telem_ctx.tcp_connections[i].remote_node == dst_node &&
            telem_ctx.tcp_connections[i].connected) {
            conn = &telem_ctx.tcp_connections[i];
            break;
        }
    }
    
    if (!conn) {
        printf("[TCP] No connection to node %d\n", dst_node);
        return false;
    }
    
    mutex_enter_blocking(&telem_ctx.sync.socket_mutex);
    
    bool result = put_sock_send(conn->sock, data, len);
    if (result) {
        conn->bytes_sent += len;
        telem_ctx.stats.packets_sent++;
    }
    
    mutex_exit(&telem_ctx.sync.socket_mutex);
    return result;
}

// ============= UDP FUNCTIONS =============
bool udp_broadcast_beacon(void) {
    winc_mesh_beacon_t beacon;
    
    beacon.node_id = telem_ctx.my_node_id;
    beacon.seq_num = telem_ctx.stats.packets_sent & 0xFFFF;
    beacon.flags = telem_ctx.iridium_gateway ? 0x01 : 0x00;
    beacon.timestamp = to_ms_since_boot(get_absolute_time());
    
    // Add CRC
    beacon.crc = calculate_crc32((uint8_t*)&beacon, 
                                 sizeof(beacon) - sizeof(uint32_t));
    
    mutex_enter_blocking(&telem_ctx.sync.socket_mutex);
    bool result = put_sock_sendto(telem_ctx.udp_sock, &beacon, sizeof(beacon));
    mutex_exit(&telem_ctx.sync.socket_mutex);
    
    if (result) {
        telem_ctx.stats.udp_broadcasts++;
    }
    
    return result;
}

bool udp_send_telemetry(uint8_t dst_node, void *data, uint16_t len, 
                        telem_priority_t priority) {
    telemetry_packet_t packet;
    
    packet.type = PKT_TELEMETRY;
    packet.priority = priority;
    packet.length = len;
    packet.timestamp = to_ms_since_boot(get_absolute_time());
    packet.src_node = telem_ctx.my_node_id;
    packet.dst_node = dst_node;
    packet.flags = 0;
    
    memcpy(packet.payload, data, MIN(len, 1024));
    
    // Add integrity
    packet.integrity.sequence = telem_ctx.stats.packets_sent & 0xFFFF;
    packet.integrity.retry_count = 0;
    packet.integrity.redundancy_id = 0;
    packet.integrity.crc32 = calculate_crc32((uint8_t*)&packet + 4, 
                                            sizeof(packet) - 4);
    
    mutex_enter_blocking(&telem_ctx.sync.socket_mutex);
    bool result = put_sock_sendto(telem_ctx.udp_sock, &packet, 
                                 sizeof(telemetry_packet_t));
    mutex_exit(&telem_ctx.sync.socket_mutex);
    
    if (result) {
        telem_ctx.stats.packets_sent++;
    }
    
    return result;
}

// ============= MULTI-CORE THREADING =============
void core1_network_handler(void) {
    printf("[CORE1] Network handler started\n");
    
    // Initialize core 1 resources
    telem_ctx.sync.core1_ready = true;
    
    uint32_t last_beacon = 0;
    uint32_t last_health = 0;
    
    while (!telem_ctx.sync.shutdown) {
        uint32_t now = to_ms_since_boot(get_absolute_time());
        
        // Send beacon every interval
        if (now - last_beacon >= telem_ctx.beacon_interval) {
            udp_broadcast_beacon();
            last_beacon = now;
        }
        
        // Check TCP connection health
        if (now - last_health >= 5000) {  // Every 5 seconds
            for (int i = 0; i < 4; i++) {
                tcp_connection_t *conn = &telem_ctx.tcp_connections[i];
                if (conn->connected && 
                    (now - conn->last_activity) > 30000) {  // 30 second timeout
                    printf("[TCP] Connection %d timed out\n", i);
                    conn->connected = false;
                    put_sock_close(conn->sock);
                }
            }
            last_health = now;
        }
        
        // Process network mode
        if (g_ctx.connection_state.is_ap_mode) {
            process_ap_mode();
        } else {
            process_p2p_mode();
        }
        
        // Process packet queues
        mutex_enter_blocking(&telem_ctx.sync.queue_mutex);
        telemetry_packet_t *tx_packet = dequeue_packet(true);
        mutex_exit(&telem_ctx.sync.queue_mutex);
        
        if (tx_packet) {
            send_with_redundancy(tx_packet);
        }
        
        // Small delay to prevent CPU hogging
        sleep_ms(10);
    }
    
    printf("[CORE1] Network handler stopped\n");
}

void process_ap_mode(void) {
    // AP-specific processing
    // Check for new client connections
    // Manage DHCP leases
    // Route packets between clients
    
    static uint32_t last_client_check = 0;
    uint32_t now = to_ms_since_boot(get_absolute_time());
    
    if (now - last_client_check >= 1000) {  // Check every second
        // Poll for client changes
        // This would interface with ATWINC1500 AP status commands
        last_client_check = now;
    }
}

void process_p2p_mode(void) {
    // P2P-specific processing
    // Maintain peer connections
    // Handle mesh routing
    // Implement hop-by-hop forwarding
    
    static uint32_t last_route_update = 0;
    uint32_t now = to_ms_since_boot(get_absolute_time());
    
    if (now - last_route_update >= 10000) {  // Update routes every 10 seconds
        // Update routing table based on received beacons
        // Clean up stale routes
        last_route_update = now;
    }
}

// ============= QUEUE MANAGEMENT =============
bool enqueue_packet(telemetry_packet_t *packet, bool is_tx) {
    mutex_enter_blocking(&telem_ctx.sync.queue_mutex);
    
    uint8_t *head = is_tx ? &telem_ctx.tx_head : &telem_ctx.rx_head;
    uint8_t *tail = is_tx ? &telem_ctx.tx_tail : &telem_ctx.rx_tail;
    telemetry_packet_t *queue = is_tx ? telem_ctx.tx_queue : telem_ctx.rx_queue;
    
    uint8_t next_head = (*head + 1) & 31;  // Circular buffer
    
    if (next_head == *tail) {
        mutex_exit(&telem_ctx.sync.queue_mutex);
        return false;  // Queue full
    }
    
    memcpy(&queue[*head], packet, sizeof(telemetry_packet_t));
    *head = next_head;
    
    mutex_exit(&telem_ctx.sync.queue_mutex);
    return true;
}

telemetry_packet_t* dequeue_packet(bool is_tx) {
    // Note: Caller must hold queue_mutex
    
    uint8_t *head = is_tx ? &telem_ctx.tx_head : &telem_ctx.rx_head;
    uint8_t *tail = is_tx ? &telem_ctx.tx_tail : &telem_ctx.rx_tail;
    telemetry_packet_t *queue = is_tx ? telem_ctx.tx_queue : telem_ctx.rx_queue;
    
    if (*tail == *head) {
        return NULL;  // Queue empty
    }
    
    telemetry_packet_t *packet = &queue[*tail];
    *tail = (*tail + 1) & 31;
    
    return packet;
}

// ============= INITIALIZATION =============
bool telemetry_init(uint8_t node_id, bool is_gateway) {
    memset(&telem_ctx, 0, sizeof(telem_ctx));
    
    telem_ctx.my_node_id = node_id;
    telem_ctx.iridium_gateway = is_gateway;
    telem_ctx.beacon_interval = 5000;  // 5 seconds
    
    // Initialize mutexes
    mutex_init(&telem_ctx.sync.socket_mutex);
    mutex_init(&telem_ctx.sync.queue_mutex);
    mutex_init(&telem_ctx.sync.stats_mutex);
    
    // Initialize base networking
    if (!winc_init(node_id, "SpaceCapsule")) {
        printf("[TELEMETRY] Failed to initialize WINC1500\n");
        return false;
    }
    
    // Wait for network ready
    if (!winc_wait_for_network(15000)) {
        printf("[TELEMETRY] Network initialization timeout\n");
        return false;
    }
    
    // Create UDP socket for broadcasts
    telem_ctx.udp_sock = open_sock_server(WINC_MESH_PORT, false, NULL);
    if (telem_ctx.udp_sock < 0) {
        printf("[TELEMETRY] Failed to create UDP socket\n");
        return false;
    }
    
    // Create TCP server socket for critical data
    if (tcp_server_init(WINC_MESH_PORT + 1) < 0) {
        printf("[TELEMETRY] Failed to create TCP server\n");
        return false;
    }
    
    // Start network handler on Core 1
    multicore_launch_core1(core1_network_handler);
    
    // Wait for Core 1 to be ready
    while (!telem_ctx.sync.core1_ready) {
        sleep_ms(10);
    }
    
    printf("[TELEMETRY] System initialized - Node %d (%s)\n",
           node_id, is_gateway ? "Gateway" : "Node");
    
    // Enable watchdog for radiation-induced resets
    watchdog_enable(8000, 1);  // 8 second timeout with pause on debug
    
    return true;
}

void telemetry_shutdown(void) {
    printf("[TELEMETRY] Shutting down...\n");
    
    // Signal Core 1 to stop
    telem_ctx.sync.shutdown = true;
    sleep_ms(100);
    
    // Close all connections
    mutex_enter_blocking(&telem_ctx.sync.socket_mutex);
    
    for (int i = 0; i < 4; i++) {
        if (telem_ctx.tcp_connections[i].connected) {
            put_sock_close(telem_ctx.tcp_connections[i].sock);
        }
    }
    
    put_sock_close(telem_ctx.udp_sock);
    put_sock_close(telem_ctx.tcp_listen_sock);
    
    mutex_exit(&telem_ctx.sync.socket_mutex);
    
    // Print final statistics
    printf("[STATS] Packets sent: %u, received: %u\n",
           telem_ctx.stats.packets_sent, telem_ctx.stats.packets_received);
    printf("[STATS] CRC errors: %u, retransmissions: %u\n",
           telem_ctx.stats.crc_errors, telem_ctx.stats.retransmissions);
}