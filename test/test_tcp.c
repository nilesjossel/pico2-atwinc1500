// TCP Socket Testing Program
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "pico/stdlib.h"
#include "winc_lib.h"
#include "winc_sock.h"

// Test configuration
#define TEST_TCP_PORT 8080
#define TEST_UDP_PORT 8081
#define TEST_DURATION_MS 60000  // 1 minute test

// Test statistics
typedef struct {
    uint32_t tcp_packets_sent;
    uint32_t tcp_packets_received;
    uint32_t tcp_bytes_sent;
    uint32_t tcp_bytes_received;
    uint32_t tcp_errors;
    uint32_t tcp_connections;
    uint32_t udp_packets_sent;
    uint32_t udp_packets_received;
    uint32_t crc_errors;
    uint32_t max_latency_ms;
    uint32_t min_latency_ms;
    uint32_t avg_latency_ms;
} test_stats_t;

static test_stats_t stats;
static int tcp_server_sock = -1;
static int tcp_client_sock = -1;
static int udp_sock = -1;

// TCP test packet
typedef struct {
    uint32_t sequence;
    uint32_t timestamp;
    uint32_t crc;
    uint8_t data[256];
} test_packet_t;

// ============= CRC CALCULATION =============
uint32_t calc_crc32_simple(const uint8_t *data, size_t len) {
    uint32_t crc = 0xFFFFFFFF;
    
    for (size_t i = 0; i < len; i++) {
        crc ^= data[i];
        for (int j = 0; j < 8; j++) {
            if (crc & 1) {
                crc = (crc >> 1) ^ 0xEDB88320;
            } else {
                crc = crc >> 1;
            }
        }
    }
    
    return ~crc;
}

// ============= TCP HANDLERS =============
void tcp_test_handler(uint8_t sock, int rxlen) {
    test_packet_t packet;
    
    printf("[TCP_TEST] Socket %d received %d bytes\n", sock, rxlen);
    
    if (rxlen <= 0) {
        printf("[TCP_TEST] Connection closed on socket %d\n", sock);
        stats.tcp_errors++;
        sock_state(sock, STATE_CLOSED);
        return;
    }
    
    // Receive packet
    if (get_sock_data(sock, &packet, rxlen)) {
        stats.tcp_packets_received++;
        stats.tcp_bytes_received += rxlen;
        
        // Verify CRC
        uint32_t calc_crc = calc_crc32_simple((uint8_t*)&packet, 
                                              sizeof(packet) - sizeof(uint32_t));
        
        if (calc_crc != packet.crc) {
            printf("[TCP_TEST] CRC error! Expected: 0x%08X, Got: 0x%08X\n",
                   packet.crc, calc_crc);
            stats.crc_errors++;
        } else {
            // Calculate latency
            uint32_t now = to_ms_since_boot(get_absolute_time());
            uint32_t latency = now - packet.timestamp;
            
            if (latency > stats.max_latency_ms) stats.max_latency_ms = latency;
            if (latency < stats.min_latency_ms || stats.min_latency_ms == 0) 
                stats.min_latency_ms = latency;
            
            stats.avg_latency_ms = (stats.avg_latency_ms + latency) / 2;
            
            printf("[TCP_TEST] Packet %u: Latency %u ms\n", 
                   packet.sequence, latency);
            
            // Echo back with updated timestamp
            packet.timestamp = now;
            packet.crc = calc_crc32_simple((uint8_t*)&packet, 
                                          sizeof(packet) - sizeof(uint32_t));
            
            if (put_sock_send(sock, &packet, sizeof(packet))) {
                stats.tcp_packets_sent++;
                stats.tcp_bytes_sent += sizeof(packet);
            } else {
                stats.tcp_errors++;
            }
        }
    }
    
    // Continue receiving
    put_sock_recv(sock);
}

void tcp_accept_handler(uint8_t listen_sock, int status) {
    printf("[TCP_TEST] New connection on listen socket %d\n", listen_sock);
    stats.tcp_connections++;
    
    // The connection is already accepted, just need to start receiving
    // Find the new socket (it will be different from listen_sock)
    for (int i = MIN_TCP_SOCK; i < MAX_TCP_SOCK; i++) {
        if (i != listen_sock && g_ctx.sockets[i].state == STATE_CONNECTED) {
            printf("[TCP_TEST] Client connected on socket %d\n", i);
            put_sock_recv(i);
            break;
        }
    }
}

// ============= UDP TEST HANDLER =============
void udp_test_handler(uint8_t sock, int rxlen) {
    test_packet_t packet;
    
    if (rxlen > 0 && get_sock_data(sock, &packet, rxlen)) {
        stats.udp_packets_received++;
        
        // Verify and echo
        uint32_t calc_crc = calc_crc32_simple((uint8_t*)&packet,
                                              sizeof(packet) - sizeof(uint32_t));
        
        if (calc_crc != packet.crc) {
            stats.crc_errors++;
        } else {
            // Update and send back
            packet.timestamp = to_ms_since_boot(get_absolute_time());
            packet.crc = calc_crc32_simple((uint8_t*)&packet,
                                          sizeof(packet) - sizeof(uint32_t));
            
            if (put_sock_sendto(sock, &packet, sizeof(packet))) {
                stats.udp_packets_sent++;
            }
        }
    }
    
    // Continue receiving
    put_sock_recvfrom(sock);
}

// ============= TEST FUNCTIONS =============
bool test_tcp_server(void) {
    printf("\n=== TCP SERVER TEST ===\n");
    
    // Create TCP server socket
    tcp_server_sock = open_sock_server(TEST_TCP_PORT, true, tcp_test_handler);
    if (tcp_server_sock < 0) {
        printf("ERROR: Failed to create TCP server on port %d\n", TEST_TCP_PORT);
        return false;
    }
    
    printf("TCP server created on socket %d, port %d\n", 
           tcp_server_sock, TEST_TCP_PORT);
    
    // Wait for socket to bind
    uint32_t start = to_ms_since_boot(get_absolute_time());
    while ((to_ms_since_boot(get_absolute_time()) - start) < 5000) {
        winc_poll();
        
        if (g_ctx.sockets[tcp_server_sock].state == STATE_BOUND) {
            printf("TCP server socket bound successfully\n");
            
            // Start listening
            put_sock_listen(tcp_server_sock);
            printf("TCP server listening for connections...\n");
            return true;
        }
        
        sleep_ms(100);
    }
    
    printf("ERROR: TCP server socket binding timeout\n");
    return false;
}

bool test_tcp_client(uint32_t server_ip) {
    printf("\n=== TCP CLIENT TEST ===\n");
    
    // Find free TCP socket
    for (int i = MIN_TCP_SOCK; i < MAX_TCP_SOCK; i++) {
        if (g_ctx.sockets[i].state == STATE_CLOSED) {
            tcp_client_sock = i;
            break;
        }
    }
    
    if (tcp_client_sock < 0) {
        printf("ERROR: No free TCP sockets for client\n");
        return false;
    }
    
    // Setup client socket
    SOCKET *sp = &g_ctx.sockets[tcp_client_sock];
    sp->addr.family = IP_FAMILY;
    sp->addr.port = swap16(TEST_TCP_PORT);
    sp->addr.ip = server_ip;
    sp->localport = TEST_TCP_PORT + 100;  // Different local port
    sp->session = rand() & 0xFFFF;
    sp->state = STATE_CONNECTING;
    sp->handler = tcp_test_handler;
    
    printf("TCP client attempting connection to %u.%u.%u.%u:%d\n",
           (server_ip >> 0) & 0xFF, (server_ip >> 8) & 0xFF,
           (server_ip >> 16) & 0xFF, (server_ip >> 24) & 0xFF,
           TEST_TCP_PORT);
    
    // Note: This requires GOP_CONNECT implementation
    // For now, we simulate with bind
    put_sock_bind(tcp_client_sock, sp->localport);
    
    return true;
}

bool test_udp(void) {
    printf("\n=== UDP TEST ===\n");
    
    udp_sock = open_sock_server(TEST_UDP_PORT, false, udp_test_handler);
    if (udp_sock < 0) {
        printf("ERROR: Failed to create UDP socket on port %d\n", TEST_UDP_PORT);
        return false;
    }
    
    printf("UDP socket created on socket %d, port %d\n", udp_sock, TEST_UDP_PORT);
    
    // Wait for binding
    uint32_t start = to_ms_since_boot(get_absolute_time());
    while ((to_ms_since_boot(get_absolute_time()) - start) < 5000) {
        winc_poll();
        
        if (g_ctx.sockets[udp_sock].state == STATE_BOUND) {
            printf("UDP socket bound successfully\n");
            put_sock_recvfrom(udp_sock);
            return true;
        }
        
        sleep_ms(100);
    }
    
    printf("ERROR: UDP socket binding timeout\n");
    return false;
}

void send_test_packets(void) {
    static uint32_t tcp_seq = 0;
    static uint32_t udp_seq = 0;
    test_packet_t packet;
    
    // TCP test packet
    if (tcp_client_sock >= 0 && 
        g_ctx.sockets[tcp_client_sock].state == STATE_CONNECTED) {
        
        packet.sequence = tcp_seq++;
        packet.timestamp = to_ms_since_boot(get_absolute_time());
        
        // Fill with test pattern
        for (int i = 0; i < sizeof(packet.data); i++) {
            packet.data[i] = (uint8_t)(i ^ packet.sequence);
        }
        
        packet.crc = calc_crc32_simple((uint8_t*)&packet,
                                       sizeof(packet) - sizeof(uint32_t));
        
        if (put_sock_send(tcp_client_sock, &packet, sizeof(packet))) {
            stats.tcp_packets_sent++;
            stats.tcp_bytes_sent += sizeof(packet);
            printf("[TCP] Sent packet %u\n", packet.sequence);
        } else {
            stats.tcp_errors++;
        }
    }
    
    // UDP test packet
    if (udp_sock >= 0 && g_ctx.sockets[udp_sock].state == STATE_BOUND) {
        packet.sequence = udp_seq++;
        packet.timestamp = to_ms_since_boot(get_absolute_time());
        
        // Different test pattern for UDP
        for (int i = 0; i < sizeof(packet.data); i++) {
            packet.data[i] = (uint8_t)(~i ^ packet.sequence);
        }
        
        packet.crc = calc_crc32_simple((uint8_t*)&packet,
                                       sizeof(packet) - sizeof(uint32_t));
        
        if (put_sock_sendto(udp_sock, &packet, sizeof(packet))) {
            stats.udp_packets_sent++;
            printf("[UDP] Sent packet %u\n", packet.sequence);
        }
    }
}

void print_statistics(void) {
    printf("\n========== TEST STATISTICS ==========\n");
    printf("TCP:\n");
    printf("  Connections:  %u\n", stats.tcp_connections);
    printf("  Packets Sent: %u\n", stats.tcp_packets_sent);
    printf("  Packets Recv: %u\n", stats.tcp_packets_received);
    printf("  Bytes Sent:   %u\n", stats.tcp_bytes_sent);
    printf("  Bytes Recv:   %u\n", stats.tcp_bytes_received);
    printf("  Errors:       %u\n", stats.tcp_errors);
    printf("\nUDP:\n");
    printf("  Packets Sent: %u\n", stats.udp_packets_sent);
    printf("  Packets Recv: %u\n", stats.udp_packets_received);
    printf("\nLatency:\n");
    printf("  Min: %u ms\n", stats.min_latency_ms);
    printf("  Max: %u ms\n", stats.max_latency_ms);
    printf("  Avg: %u ms\n", stats.avg_latency_ms);
    printf("\nErrors:\n");
    printf("  CRC Errors: %u\n", stats.crc_errors);
    printf("=====================================\n");
}

// ============= MAIN TEST PROGRAM =============
int main(void) {
    stdio_init_all();
    sleep_ms(2000);
    
    printf("\n=====================================\n");
    printf("TCP/UDP SOCKET TEST PROGRAM\n");
    printf("=====================================\n");
    
    // Initialize statistics
    memset(&stats, 0, sizeof(stats));
    
    // Determine node role
    uint8_t node_id = 1;
    gpio_init(15);
    gpio_set_dir(15, GPIO_IN);
    gpio_pull_up(15);
    sleep_ms(10);
    
    if (gpio_get(15) == 0) {
        node_id = 2;
    }
    
    printf("Node ID: %d\n", node_id);
    printf("Test Role: %s\n", node_id == 1 ? "SERVER" : "CLIENT");
    
    // Initialize WINC1500
    if (!winc_init(node_id, "TCPTest")) {
        printf("ERROR: Failed to initialize WINC1500\n");
        while (1) sleep_ms(1000);
    }
    
    // Wait for network
    printf("Waiting for network...\n");
    if (!winc_wait_for_network(15000)) {
        printf("ERROR: Network initialization timeout\n");
        while (1) sleep_ms(1000);
    }
    
    printf("Network ready!\n");
    
    // Start tests based on role
    if (node_id == 1) {
        // Server mode
        if (!test_tcp_server()) {
            printf("TCP server test failed\n");
        }
        
        if (!test_udp()) {
            printf("UDP test failed\n");
        }
        
        printf("\nServer ready, waiting for client connections...\n");
    } else {
        // Client mode
        sleep_ms(3000);  // Let server start first
        
        // Try to connect to server (192.168.1.1 in AP mode)
        uint32_t server_ip = 0xC0A80101;  // 192.168.1.1
        
        if (!test_tcp_client(server_ip)) {
            printf("TCP client test failed\n");
        }
        
        if (!test_udp()) {
            printf("UDP test failed\n");
        }
    }
    
    // Main test loop
    uint32_t test_start = to_ms_since_boot(get_absolute_time());
    uint32_t last_send = 0;
    uint32_t last_stats = 0;
    
    printf("\nRunning tests for %d seconds...\n", TEST_DURATION_MS / 1000);
    
    while ((to_ms_since_boot(get_absolute_time()) - test_start) < TEST_DURATION_MS) {
        uint32_t now = to_ms_since_boot(get_absolute_time());
        
        // Poll network
        winc_poll();
        
        // Send test packets every second (client only)
        if (node_id == 2 && (now - last_send) >= 1000) {
            send_test_packets();
            last_send = now;
        }
        
        // Print statistics every 10 seconds
        if ((now - last_stats) >= 10000) {
            print_statistics();
            last_stats = now;
        }
        
        sleep_ms(10);
    }
    
    // Final statistics
    printf("\n=== TEST COMPLETE ===\n");
    print_statistics();
    
    // Calculate success rate
    float tcp_success = (stats.tcp_packets_sent > 0) ? 
        ((float)stats.tcp_packets_received / stats.tcp_packets_sent) * 100 : 0;
    float udp_success = (stats.udp_packets_sent > 0) ? 
        ((float)stats.udp_packets_received / stats.udp_packets_sent) * 100 : 0;
    
    printf("\nSuccess Rates:\n");
    printf("  TCP: %.1f%%\n", tcp_success);
    printf("  UDP: %.1f%%\n", udp_success);
    
    if (tcp_success > 95.0 && udp_success > 90.0 && stats.crc_errors == 0) {
        printf("\n*** ALL TESTS PASSED ***\n");
    } else {
        printf("\n*** TESTS FAILED ***\n");
    }
    
    while (1) {
        sleep_ms(1000);
    }
    
    return 0;
}