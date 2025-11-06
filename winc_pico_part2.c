#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <unistd.h>
#include <stdbool.h>
#include "pico/stdlib.h"
#include "hardware/spi.h"
#include "winc_wifi.h"
#include "winc_sock.h"
#include "winc_p2p.h"

#define VERBOSE     1           // Diagnostic output level (0 to 3)
#define SPI_SPEED   11000000    // SPI clock (actually 10.42 MHz)
#define SPI_PORT    spi0        // SPI port number
#define NEW_PROTO   1           // Old or new Pico connections

// Mode selection: set to 1 to enable mesh mode, 0 for standard WiFi mode
#define ENABLE_MESH_MODE 1
#define MESH_NODE_ID     1      // Unique ID for this mesh node (1-255)
#define MESH_NODE_NAME   "PicoNode1"

#define SCK_PIN     18
#define MOSI_PIN    19
#define MISO_PIN    16
#define CS_PIN      17
#define WAKE_PIN    20
#define RESET_PIN   21
#define IRQ_PIN     22

#define PSK_SSID            "testnet"
#define PSK_PASSPHRASE      "testpass"

extern int verbose;

// Return microsecond time
uint32_t usec(void)
{
    return(time_us_32());
}

// Do SPI transfer
int spi_xfer(int fd, uint8_t *txd, uint8_t *rxd, int len)
{
    if (verbose > 2)
    {
        printf("  Tx:");
        for (int i=0; i<len; i++)
            printf(" %02X", txd[i]);
    }
    gpio_put(CS_PIN, 0);
    spi_write_read_blocking(SPI_PORT, txd, rxd, len);
    while (gpio_get(SCK_PIN)) ;
    gpio_put(CS_PIN, 1);
    if (verbose > 2)
    {
        printf("\n  Rx:");
        for (int i=0; i<len; i++)
            printf(" %02X", rxd[i]);
        printf("\n");
    }
    return(len);
}

// Read IRQ line
int read_irq(void)
{
    return(gpio_get(IRQ_PIN));
}

// Initialise SPI interface
void spi_setup(int fd)
{
    stdio_init_all();
    spi_init(SPI_PORT, SPI_SPEED);
    spi_set_format(SPI_PORT, 8, SPI_CPOL_0, SPI_CPHA_0, SPI_MSB_FIRST);
    gpio_init(MISO_PIN);
    gpio_set_function(MISO_PIN, GPIO_FUNC_SPI);
    gpio_set_function(CS_PIN,   GPIO_FUNC_SIO);
    gpio_set_function(SCK_PIN,  GPIO_FUNC_SPI);
    gpio_set_function(MOSI_PIN, GPIO_FUNC_SPI);
    gpio_init(CS_PIN);
    gpio_set_dir(CS_PIN, GPIO_OUT);
    gpio_put(CS_PIN, 1);
    gpio_init(WAKE_PIN);
    gpio_set_dir(WAKE_PIN, GPIO_OUT);
    gpio_put(WAKE_PIN, 1);
    gpio_init(IRQ_PIN);
    gpio_set_dir(IRQ_PIN, GPIO_IN);
    gpio_pull_up(IRQ_PIN);
    gpio_init(RESET_PIN);
    gpio_set_dir(RESET_PIN, GPIO_OUT);
    gpio_put(RESET_PIN, 0);
    sleep_ms(1);
    gpio_put(RESET_PIN, 1);
    sleep_ms(1);
}

int main(int argc, char *argv[])
{
    int fd;
    uint32_t val=0;
    bool ok, irq=1;
    int sock;

    verbose = VERBOSE;
    spi_setup(fd);
    disable_crc(fd);
    ok = chip_init(fd);
    if (!ok)
        printf("Can't initialise chip\n");
    else
    {
        ok = chip_get_info(fd);
        ok = ok && set_gpio_val(fd, 0x58070) && set_gpio_dir(fd, 0x58070);

#if ENABLE_MESH_MODE
        printf("\n=== Mesh Networking Mode ===\n");
        printf("Node ID: %u\n", MESH_NODE_ID);
        printf("Node Name: %s\n", MESH_NODE_NAME);
        printf("===========================\n\n");

        // Initialize P2P mode
        ok = ok && p2p_enable(fd, P2P_LISTEN_CHAN);

        if (ok)
        {
            // Initialize mesh networking
            ok = mesh_init(fd, MESH_NODE_ID, MESH_NODE_NAME);
            ok = ok && mesh_enable(fd);

            if (ok)
                printf("Mesh networking enabled\n");
            else
                printf("Failed to enable mesh networking\n");
        }
        else
        {
            printf("Failed to enable P2P mode\n");
        }

        // Set up UDP socket for mesh communication
        sock = open_sock_server(UDP_PORTNUM, 0, udp_echo_handler);
        printf("Mesh socket %u UDP port %u %s\n", sock, UDP_PORTNUM, sock>=0 ? "ok" : "failed");

        // Also support TCP for mesh data
        sock = open_sock_server(TCP_PORTNUM, 1, tcp_echo_handler);
        printf("Mesh socket %u TCP port %u %s\n", sock, TCP_PORTNUM, sock>=0 ? "ok" : "failed");

        // In mesh mode, we don't join a traditional WiFi network
        // Instead, we establish P2P connections
        printf("\nWaiting for P2P connections...\n");
        printf("Press any key to print routing table\n\n");

#else
        // Standard WiFi mode
        printf("\n=== Standard WiFi Mode ===\n");

        sock = open_sock_server(TCP_PORTNUM, 1, tcp_echo_handler);
        printf("Socket %u TCP port %u %s\n", sock, TCP_PORTNUM, sock>=0 ? "ok" : "failed");
        sock = open_sock_server(UDP_PORTNUM, 0, udp_echo_handler);
        printf("Socket %u UDP port %u %s\n", sock, UDP_PORTNUM, sock>=0 ? "ok" : "failed");

        ok = join_net(fd, PSK_SSID, PSK_PASSPHRASE);

        printf("Connecting");
        while (ok && (irq=read_irq()) && msdelay(100))
        {
            putchar('.');
            fflush(stdout);
        }
        printf("\n");
#endif

        // Main loop
        uint32_t last_print = 0;
        while (ok)
        {
            if (read_irq() == 0)
                interrupt_handler();

#if ENABLE_MESH_MODE
            // Handle mesh beacon sending and routing table maintenance
            mesh_beacon_handler(fd);

            // Periodically print routing table (every 30 seconds)
            uint32_t now = time_us_32() / 1000;
            if (now - last_print > 30000)
            {
                mesh_print_routing_table();
                last_print = now;
            }
#endif
        }
    }
	return(0);
}

// EOF
