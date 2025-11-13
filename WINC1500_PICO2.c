#include <stdio.h>
#include <string.h>
#include "pico/stdlib.h"
#include "hardware/spi.h"
#include "hardware/gpio.h"
#include "winc_lib.h"

/* Node configuration - CHANGE THESE FOR EACH NODE */
#ifndef MY_NODE_ID
#define MY_NODE_ID      1
#endif

#ifndef MY_NODE_NAME
#define MY_NODE_NAME    "Pico1"
#endif

// Target node: Node 1 sends to Node 2, Node 2 sends to Node 1
#ifndef TARGET_NODE_ID
#define TARGET_NODE_ID  (MY_NODE_ID == 1 ? 2 : 1)
#endif

/* LED pin - onboard LED on Pico/Pico2 */
#define LED_PIN 25

int main(void) {
    /* Initialize standard I/O */
    stdio_init_all();

    /* Initialize LED */
    gpio_init(LED_PIN);
    gpio_set_dir(LED_PIN, GPIO_OUT);

    /* Blink LED 3 times rapidly to show we're starting */
    for (int i = 0; i < 3; i++) {
        gpio_put(LED_PIN, 1);
        sleep_ms(100);
        gpio_put(LED_PIN, 0);
        sleep_ms(100);
    }

    /* Wait longer for USB serial to be ready */
    sleep_ms(2000);

    printf("\n\n=========================\n");
    printf("WINC1500 Mesh Node Example\n");
    printf("=========================\n");
    printf("Board is alive and running!\n");
    printf("Starting WINC initialization...\n\n");

    /* Initialize WINC1500 */
    bool winc_ok = winc_init(MY_NODE_ID, MY_NODE_NAME);

    if (!winc_ok) {
        printf("ERROR: WINC initialization failed!\n");
        printf("Please check:\n");
        printf("- WINC1500 module connections\n");
        printf("- Power supply (3.3V)\n");
        printf("- SPI pins configuration\n");
        printf("\nContinuing anyway with LED blinking...\n");
    } else {
        printf("WINC1500 initialized successfully\n");
        printf("Node ID: %d\n", MY_NODE_ID);
        printf("Name: %s\n", MY_NODE_NAME);
        printf("Will send to Node ID: %d\n", TARGET_NODE_ID);
    }

    /* Set up callback for received mesh messages */
    void mesh_callback(uint8_t src_node, uint8_t *data, uint16_t len) {
        printf("\n>>> Received from Node %d: %.*s\n", src_node, len, data);
        // Blink LED twice when message received
        for (int i = 0; i < 2; i++) {
            gpio_put(LED_PIN, 1);
            sleep_ms(50);
            gpio_put(LED_PIN, 0);
            sleep_ms(50);
        }
    }

    if (winc_ok) {
        winc_mesh_set_callback(mesh_callback);
    }

    /* Main loop */
    uint32_t blink_counter = 0;
    uint32_t print_counter = 0;
    uint32_t send_counter = 0;

    while (1) {
        /* Blink LED - fast if WINC failed, slow if WINC OK */
        if (blink_counter % (winc_ok ? 1000 : 250) == 0) {
            gpio_put(LED_PIN, !gpio_get(LED_PIN));
        }

        /* Print status every 10 seconds */
        if (print_counter % 10000 == 0) {
            printf("Heartbeat: %lu seconds | WINC: %s | Nodes: %d\n",
                   print_counter / 1000,
                   winc_ok ? "OK" : "FAILED",
                   winc_ok ? winc_mesh_get_node_count() : 0);
        }

        /* Send message every 5 seconds if WINC is OK */
        if (winc_ok && send_counter % 5000 == 0 && send_counter > 0) {
            char message[64];
            snprintf(message, sizeof(message), "Hello from Node %d! Time: %lu",
                     MY_NODE_ID, send_counter / 1000);

            if (winc_mesh_send(TARGET_NODE_ID, (uint8_t*)message, strlen(message))) {
                printf("<<< Sent to Node %d: %s\n", TARGET_NODE_ID, message);
            } else {
                printf("Failed to send to Node %d\n", TARGET_NODE_ID);
            }
        }

        if (winc_ok) {
            winc_poll();  /* Handle WiFi events */
        }

        sleep_ms(1);
        blink_counter++;
        print_counter++;
        send_counter++;
    }

    return 0; /* Never reached */
}
