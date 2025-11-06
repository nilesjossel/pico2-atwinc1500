// ATWINC1500 Mesh Network Example
// Simple P2P mesh node for Raspberry Pi Pico / Pico 2
//
// Usage:
// 1. Set MY_NODE_ID to unique value for each board (1, 2, 3, etc.)
// 2. Compile and flash to multiple Pico boards
// 3. Power on - they will discover each other and form mesh
// 4. Send messages between nodes

#include <stdio.h>
#include <string.h>
#include "pico/stdlib.h"
#include "winc_lib.h"

// ============================================================================
// CONFIGURATION - CHANGE FOR EACH BOARD
// ============================================================================

#ifndef MY_NODE_ID
#define MY_NODE_ID  1  // Change to 2, 3, 4, etc. for other boards
#endif

#ifndef MY_NODE_NAME
#define MY_NODE_NAME "Pico1"  // Change to "Pico2", "Pico3", etc.
#endif

// Target node for test messages (set to 0 to disable auto-send)
#ifndef TARGET_NODE
#define TARGET_NODE 2  // Send test messages to this node
#endif

// Send test message every N milliseconds (0 to disable)
#ifndef TEST_SEND_INTERVAL
#define TEST_SEND_INTERVAL 10000  // 10 seconds
#endif

// ============================================================================
// MESH DATA HANDLER
// ============================================================================

void mesh_data_received(uint8_t src_node, uint8_t *data, uint16_t len)
{
    // Print received data
    printf("\n=== MESH DATA RECEIVED ===\n");
    printf("From: Node %u\n", src_node);
    printf("Length: %u bytes\n", len);
    printf("Data: ");

    // Print as string if printable, hex otherwise
    bool is_text = true;
    for (uint16_t i = 0; i < len; i++) {
        if (data[i] < 32 || data[i] > 126) {
            is_text = false;
            break;
        }
    }

    if (is_text) {
        printf("%.*s\n", len, data);
    } else {
        for (uint16_t i = 0; i < len; i++) {
            printf("%02X ", data[i]);
            if ((i + 1) % 16 == 0) printf("\n     ");
        }
        printf("\n");
    }

    printf("==========================\n\n");

    // Echo back to sender (optional)
    // char reply[64];
    // snprintf(reply, sizeof(reply), "ACK from node %u", MY_NODE_ID);
    // winc_mesh_send(src_node, (uint8_t*)reply, strlen(reply));
}

// ============================================================================
// MAIN
// ============================================================================

int main()
{
    // Initialize stdio
    stdio_init_all();

    // Wait for USB serial (optional, for debugging)
    #ifdef PICO_DEFAULT_LED_PIN
    gpio_init(PICO_DEFAULT_LED_PIN);
    gpio_set_dir(PICO_DEFAULT_LED_PIN, GPIO_OUT);
    gpio_put(PICO_DEFAULT_LED_PIN, 1);
    #endif

    // Startup message
    printf("\n\n");
    printf("========================================\n");
    printf("  ATWINC1500 P2P Mesh Network Node\n");
    printf("========================================\n");
    printf("Node ID:   %u\n", MY_NODE_ID);
    printf("Node Name: %s\n", MY_NODE_NAME);
    printf("P2P Chan:  %u\n", WINC_P2P_CHANNEL);
    printf("Mesh Port: %u\n", WINC_MESH_PORT);
    printf("========================================\n\n");

    // Initialize WINC1500 mesh
    printf("Initializing WINC1500...\n");
    if (!winc_init(MY_NODE_ID, MY_NODE_NAME)) {
        printf("ERROR: WINC initialization failed!\n");
        printf("Check:\n");
        printf("  - WINC1500 module connected?\n");
        printf("  - Correct pins (SCK=%u, MOSI=%u, MISO=%u, CS=%u)?\n",
               WINC_PIN_SCK, WINC_PIN_MOSI, WINC_PIN_MISO, WINC_PIN_CS);
        printf("  - 3.3V power supply OK?\n");
        return -1;
    }

    printf("WINC1500 initialized OK\n");

    // Print firmware version
    uint8_t fw_major, fw_minor, fw_patch;
    winc_get_firmware_version(&fw_major, &fw_minor, &fw_patch);
    printf("Firmware: %u.%u.%u\n", fw_major, fw_minor, fw_patch);

    // Print MAC address
    uint8_t mac[6];
    winc_get_mac(mac);
    printf("MAC: %02X:%02X:%02X:%02X:%02X:%02X\n",
           mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);

    // Set mesh callback
    winc_mesh_set_callback(mesh_data_received);

    printf("\n=== MESH NETWORK ACTIVE ===\n");
    printf("Listening for P2P connections...\n");
    printf("Sending beacons every %u ms\n", WINC_MESH_BEACON_INTERVAL_MS);
    #if TARGET_NODE > 0 && TEST_SEND_INTERVAL > 0
    printf("Sending test messages to node %u every %u ms\n",
           TARGET_NODE, TEST_SEND_INTERVAL);
    #endif
    printf("===========================\n\n");

    // Main loop
    uint32_t last_send = 0;
    uint32_t last_status = 0;
    uint32_t loop_count = 0;

    while (true)
    {
        // Poll for events (handles IRQ, beacons, routing)
        winc_poll();

        uint32_t now = to_ms_since_boot(get_absolute_time());

        // Send test message periodically
        #if TARGET_NODE > 0 && TEST_SEND_INTERVAL > 0
        if (now - last_send >= TEST_SEND_INTERVAL) {
            char msg[128];
            snprintf(msg, sizeof(msg),
                     "Hello from node %u (%s) - message #%lu",
                     MY_NODE_ID, MY_NODE_NAME, loop_count);

            printf("Sending to node %u: %s\n", TARGET_NODE, msg);

            if (winc_mesh_send(TARGET_NODE, (uint8_t*)msg, strlen(msg))) {
                printf("  -> Sent OK\n");
            } else {
                printf("  -> Failed (no route?)\n");
            }

            last_send = now;
        }
        #endif

        // Print status every 30 seconds
        if (now - last_status >= 30000) {
            printf("\n--- Status (loop %lu) ---\n", loop_count);
            printf("Uptime: %lu seconds\n", now / 1000);
            printf("Active routes: %u\n", winc_mesh_get_node_count());
            winc_mesh_print_routes();
            printf("------------------------\n\n");

            last_status = now;
        }

        loop_count++;

        // Small delay to prevent busy-waiting
        sleep_ms(1);
    }

    return 0;
}
