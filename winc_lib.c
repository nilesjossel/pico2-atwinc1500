/* Integrated WINC1500 library for mesh networking winc_wifi and winc_sock
adapted for global use as simplified API for Pico microcontroller platform */

#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include "pico/stdlib.h"
#include "hardware/spi.h"
#include "hardware/gpio.h"
#include "winc_lib.h"


// Header for incoming HIF message
typedef struct {
    uint8_t gid, op;
    uint16_t len;
} HIF_HDR;

// Socket address (network byte order)
typedef struct {
    uint16_t family, port;
    uint32_t ip;
} SOCK_ADDR;

// Socket handler callback
typedef void (* SOCK_HANDLER)(uint8_t sock, int rxlen);

// Socket bind command
typedef struct {
    SOCK_ADDR saddr;
    uint8_t sock, x;
    uint16_t session;
} BIND_CMD;

// Socket listen command
typedef struct {
    uint8_t sock, backlog;
    uint16_t session;
} LISTEN_CMD;

// Socket recv/recvfrom commands
typedef struct {
    uint32_t timeout;
    uint8_t sock, x;
    uint16_t session;
} RECV_CMD, RECVFROM_CMD;

// Socket sendto command
typedef struct {
    uint8_t sock, x;
    uint16_t len;
    SOCK_ADDR saddr;
    uint16_t session, x2;
} SENDTO_CMD;

// Socket close command
typedef struct {
    uint8_t sock, x;
    uint16_t session;
} CLOSE_CMD;

// Response messages
typedef struct {
    uint32_t self, gate, dns, mask, lease;
} DHCP_RESP_MSG;

typedef struct {
    uint8_t sock, status;
    uint16_t session;
} BIND_RESP_MSG;

typedef struct {
    SOCK_ADDR addr;
    uint8_t listen_sock, conn_sock;
    uint16_t oset;
} ACCEPT_RESP_MSG;

typedef struct {
    SOCK_ADDR addr;
    int16_t dlen;
    uint16_t oset;
    uint8_t sock, x;
    uint16_t session;
} RECV_RESP_MSG;

// Response message union
typedef union {
    uint8_t data[16];
    int val;
    DHCP_RESP_MSG dhcp;
    BIND_RESP_MSG bind;
    ACCEPT_RESP_MSG accept;
    RECV_RESP_MSG recv;
} RESP_MSG;

// Socket storage structure
typedef struct {
    SOCK_ADDR addr;
    uint16_t localport, session;
    int state, conn_sock;
    uint32_t hif_data_addr;
    SOCK_HANDLER handler;
} SOCKET;

// P2P enable command
typedef struct {
    uint8_t channel;
} P2P_ENABLE_CMD;
// Forward declarations for functions used before definition
static void sock_state(uint8_t sock, int news);

// Forward declarations for functions implemented in winc_mesh.c
bool winc_mesh_init(uint8_t node_id, const char *node_name);
void winc_mesh_process(void);

// ===== GLOBAL CONTEXT (some from winc_sock)=====
typedef struct {
    // Hardware pins
    struct {
        uint8_t sck, mosi, miso, cs, wake, reset, irq;
    } pins;

    // SPI buffer
    uint8_t txbuf[1600];
    uint8_t rxbuf[1600];
    uint8_t tx_zeros[1024];
    
    // Socket 
    SOCKET sockets[10];
    uint8_t databuf[1600];
    RESP_MSG resp_msg;

    // Config
    int verbose;
    bool use_crc;

    // Firmware
    uint8_t fw_major, fw_minor, fw_patch;
    uint8_t mac[6];

    // Mesh state
    struct {
        uint8_t my_node_id;
        char my_name[16];
        bool enabled;
        int udp_socket;

        // Routing table
        struct {
            uint8_t node_id;
            uint8_t next_hop;
            uint8_t hop_count;
            uint32_t last_seen;
            bool active;
        } routes[8];
        uint8_t route_count;

        uint16_t seq_num;
        uint32_t last_beacon;

        // Data callback
        void (*data_callback)(uint8_t, uint8_t*, uint16_t);
    } mesh;

    // Connection state tracking
    struct {
        bool connected;
        bool dhcp_done;
        bool ap_mode;
        uint32_t my_ip;
    } connection_state;
} winc_ctx_t;

winc_ctx_t g_ctx;    // Global context (accessible to winc_mesh.c)

// ====== UTILITY FUNCTIONS (from winc_wifi and winc_sock) ====== 
typedef struct {uint8_t cmd, addr[3], zeros[7];} CMD_MSG_A;
typedef struct {uint8_t cmd, addr[3], count[3];} CMD_MSG_B;
typedef struct {uint8_t cmd, addr[2], data[4], zeros[2];} CMD_MSG_C;
typedef struct {uint8_t cmd, addr[3], data[4], zeros[2];} CMD_MSG_D;

// Connection headers
typedef struct {
    uint16_t cred_size;
    uint8_t flags, chan, ssid_len;
    char ssid[39];
    uint8_t auth, x[3];
} CONN_HDR;

typedef struct {
    uint8_t len;
    char phrase[0x63], x[8];
} PSK_DATA;

typedef struct {
    char psk[65], typ, x1[2];
    uint16_t chan;
    char ssid[33];
    uint8_t nosave, x2[2];
} OLD_CONN_HDR;

typedef struct {
    int op;
    char *s;
} OP_STR;

static OP_STR wifi_gop_resps[] = {{GOP_CONN_REQ_OLD, "Conn req"}, {GOP_STATE_CHANGE, "State change"},
    {GOP_DHCP_CONF, "DHCP conf"}, {GOP_CONN_REQ_NEW, "Conn_req"}, {GOP_BIND, "Bind"},
    {GOP_LISTEN, "Listen"}, {GOP_ACCEPT, "Accept"}, {GOP_SEND, "Send"}, {GOP_RECV, "Recv"},
    {GOP_SENDTO, "SendTo"}, {GOP_RECVFROM, "RecvFrom"}, {GOP_CLOSE, "Close"}, {0,""}};

static uint8_t remove_crc[11] = {0xC9, 0, 0xE8, 0x24, 0,  0,  0, 0x52, 0x5C, 0, 0};

static char *sock_errs[] = {"OK", "Invalid addr", "Addr already in use",
    "Too many TCP socks", "Too many UDP socks", "?", "Invalid arg",
    "Too many listening socks", "?", "Invalid operation", "?",
    "Addr required", "Client closed", "Sock timeout", "Sock buffer full"};

static uint32_t usec(void) {
    return to_us_since_boot(get_absolute_time());
}

static char *op_str(int gid, int op) {
    OP_STR *ops = wifi_gop_resps;
    uint16_t gop = GIDOP(gid, op);
    while (ops->op && ops->op != gop)
        ops++;
    return (ops->op ? ops->s : "");
}

static char *gop_str(uint16_t gop) {
    OP_STR *ops = wifi_gop_resps;
    while (ops->op && ops->op != gop)
        ops++;
    return (ops->op ? ops->s : "");
}

static bool ustimeout(uint32_t *tp, uint32_t tout) {
    bool ret = 1;
    uint32_t t = usec();
    if (tout == 0)
        *tp = t;
    else if (t >= *tp + tout)
        *tp += tout;
    else
        ret = 0;
    return ret;
}

static bool msdelay(int n) {
    uint32_t tim;
    ustimeout(&tim, 0);
    while (!ustimeout(&tim, n * 1000));
    return 1;
}

static bool usdelay(int n) {
    uint32_t tim;
    ustimeout(&tim, 0);
    while (!ustimeout(&tim, n));
    return 1;
}

static void dump_hex(uint8_t *data, int dlen, int ncols, char *indent) {
    int i;
    printf("%s", indent);
    for (i = 0; i < dlen; i++) {
        if (ncols && (i && i % ncols == 0))
            printf("\n%s", i == dlen - 1 ? "" : indent);
        printf("%02X ", *data++);
    }
    printf("\n");
}

static uint16_t swap16(uint16_t val) {
    return ((val >> 8) | ((val & 0xff) << 8));
}

// SPI Functions
static int spi_xfer(uint8_t *txd, uint8_t *rxd, int len) {
    gpio_put(g_ctx.pins.cs, 0);
    spi_write_read_blocking(spi0, txd, rxd, len);
    gpio_put(g_ctx.pins.cs, 1);
    return len;
}

static void disable_crc(void) {
    spi_xfer(remove_crc, g_ctx.rxbuf, sizeof(remove_crc));
    g_ctx.use_crc = 0;
}

static int spi_cmd_resp(uint8_t *txd, uint8_t *rxd, int txlen, int rxlen) {
    return spi_xfer(txd, rxd, txlen + rxlen);
}

static int spi_read_reg(uint32_t addr, uint32_t *valp) {
    CMD_MSG_A *mp = (CMD_MSG_A *)g_ctx.txbuf;
    int n, a, rxlen = sizeof(mp->zeros), txlen = sizeof(*mp) - rxlen;
    uint8_t *rsp = &g_ctx.rxbuf[txlen];

    mp->cmd = addr <= 0x30 ? CMD_INTERNAL_READ : CMD_SINGLE_READ;
    a = addr <= 0x30 ? (addr | CLOCKLESS_ADDR) << 8 : addr;
    U24_DATA(mp->addr, 0, a);
    memset(mp->zeros, 0, sizeof(mp->zeros));
    n = spi_cmd_resp(g_ctx.txbuf, g_ctx.rxbuf, txlen, rxlen);
    if (n && rsp[0] == mp->cmd && rsp[1] == 0 && (rsp[2] & 0xf0) == 0xf0) {
        *valp = RSP_U32(rsp, 3);
        if (g_ctx.verbose > 1)
            printf("Rd reg %04x: %08x\n", addr, *valp);
    }
    return rxlen;
}

static int spi_read_data(uint32_t addr, uint8_t *data, int dlen) {
    CMD_MSG_B *mp = (CMD_MSG_B *)g_ctx.txbuf;
    int n, tries, txlen = sizeof(*mp);
    uint8_t b;

    mp->cmd = CMD_READ_DATA;
    U24_DATA(mp->addr, 0, addr);
    U24_DATA(mp->count, 0, dlen);
    n = spi_cmd_resp((uint8_t *)mp, g_ctx.rxbuf, txlen, 0);
    b = 0;
    tries = 10;
    while (n && !b && tries--)
        n = spi_xfer(g_ctx.tx_zeros, &b, 1);
    if (n && b == CMD_READ_DATA) {
        n = spi_xfer(g_ctx.tx_zeros, data, 2) &&
            spi_xfer(g_ctx.tx_zeros, data, dlen);
        if (g_ctx.verbose > 1)
            printf("Rd data %04x: %u bytes\n", addr, dlen);
    }
    return n;
}

static int spi_write_reg(uint32_t addr, uint32_t val) {
    CMD_MSG_D *mp = (CMD_MSG_D *)g_ctx.txbuf;
    int n = 0, rxlen = sizeof(mp->zeros), txlen = sizeof(*mp) - rxlen;
    uint8_t *rsp = &g_ctx.rxbuf[txlen];

    mp->cmd = CMD_SINGLE_WRITE;
    U24_DATA(mp->addr, 0, addr);
    U32_DATA(mp->data, 0, val);
    memset(mp->zeros, 0, sizeof(mp->zeros));
    n = spi_cmd_resp((uint8_t *)mp, g_ctx.rxbuf, txlen, rxlen);
    n = rsp[0] == mp->cmd && rsp[1] == 0 ? n : 0;
    if (n && g_ctx.verbose > 1)
        printf("Wr reg %04x: %08x\n", addr, val);
    return n;
}

static int spi_write_data(uint32_t addr, uint8_t *data, int dlen) {
    CMD_MSG_B *mp = (CMD_MSG_B *)g_ctx.txbuf;
    int n, tries, txlen = sizeof(*mp);
    uint8_t b;

    mp->cmd = CMD_WRITE_DATA;
    U24_DATA(mp->addr, 0, addr);
    U24_DATA(mp->count, 0, dlen);
    g_ctx.txbuf[txlen] = g_ctx.txbuf[txlen + 1] = 0;
    g_ctx.rxbuf[0] = 0;
    n = spi_cmd_resp((uint8_t *)mp, g_ctx.rxbuf, txlen, 2) && g_ctx.rxbuf[txlen] == CMD_WRITE_DATA;
    g_ctx.txbuf[0] = 0xf3;
    memcpy(&g_ctx.txbuf[1], data, dlen);
    n = n && spi_cmd_resp(g_ctx.txbuf, g_ctx.rxbuf, dlen + 1, 0);
    b = 0;
    tries = 10;
    while (n && b != 0xc3 && tries--)
        n = spi_xfer(g_ctx.tx_zeros, &b, 1);
    n = n && spi_xfer(g_ctx.tx_zeros, &b, 1);
    if (n && g_ctx.verbose > 1)
        printf("Wr data %04x: %u bytes\n", addr, dlen);
    return n;
}

// Chip Functions
static bool chip_interrupt_enable(void) {
    uint32_t val;

    return (spi_read_reg(PIN_MUX_REG0, &val) &&
            spi_write_reg(PIN_MUX_REG0, val | 0x100) &&
            spi_read_reg(NMI_EN_REG, &val) &&
            spi_write_reg(NMI_EN_REG, val | 0x10000));
}
static bool set_gpio_dir(uint32_t dir) {
    return spi_write_reg(0x020108, dir);
}

static bool set_gpio_val(uint32_t val) {
    return spi_write_reg(0x020100, val);
}

static uint32_t chip_get_id(void) {
    uint32_t ret = 0, chip = 0, rev = 0;
    if (spi_read_reg(CHIPID_REG, &chip) &&
        spi_read_reg(REVID_REG, &rev))
        ret = chip;
    return ret;
}

static bool chip_init(void) {
    uint32_t val;
    int tries, ok;

    // Wait until EFuse values have been loaded
    tries = 10;
    do {
        ok = spi_read_reg(EFUSE_REG, &val) && (val & (1 << 31));
    } while (!ok && tries-- && msdelay(1));
    
    // Wait for bootrom
    ok = ok && spi_read_reg(HOST_WAIT_REG, &val);
    if (ok && (val & 1) == 0) {
        tries = 3;
        do {
            ok = spi_read_reg(BOOTROM_REG, &val) && val == FINISH_BOOT_VAL;
        } while (!ok && tries-- && msdelay(1));
    }
    
    ok = ok && spi_write_reg(NMI_STATE_REG, DRIVER_VER_INFO);   // Specify driver version
    ok = ok && spi_write_reg(NMI_GP_REG1, CONF_VAL);            // Set configuration
    ok = ok && spi_write_reg(BOOTROM_REG, START_FIRMWARE);      // Start firmware
    // Wait until running
    tries = 20;
    if (ok) do {
        ok = spi_read_reg(NMI_STATE_REG, &val) && val == FINISH_INIT_VAL;
    } while (!ok && tries-- && msdelay(10));
    ok = ok && spi_write_reg(NMI_STATE_REG, 0);
    ok = ok && chip_interrupt_enable();
    return ok;
}

static bool chip_get_info(void) {
    uint32_t val;
    uint16_t data[4];
    uint8_t info[40];
    bool ok;

    ok = spi_read_reg(NMI_GP_REG2, &val);
    ok = ok && spi_read_data(val | 0x30000, (uint8_t *)data, sizeof(data));
    ok = ok && spi_read_data(data[2] | 0x30000, info, sizeof(info));
    ok = ok && spi_read_data(data[1] | 0x30000, g_ctx.mac, sizeof(g_ctx.mac));
    
    g_ctx.fw_major = info[4];
    g_ctx.fw_minor = info[5];
    g_ctx.fw_patch = info[6];
    
    printf("Firmware %u.%u.%u, ", g_ctx.fw_major, g_ctx.fw_minor, g_ctx.fw_patch);
    printf("OTP MAC address %02X:%02X:%02X:%02X:%02X:%02X\n",
           g_ctx.mac[0], g_ctx.mac[1], g_ctx.mac[2], g_ctx.mac[3], g_ctx.mac[4], g_ctx.mac[5]);
    return ok;
}

// HIF Functions
static bool hif_start(uint8_t gid, uint8_t op, int dlen) {
    uint32_t val, tries = 100, len = 8 + dlen;
    uint8_t hif[4] = {(uint8_t)(len >> 8), (uint8_t)len, op, gid};
    bool ok;

    ok = spi_write_reg(NMI_STATE_REG, DATA_U32(hif)) &&
         spi_write_reg(RCV_CTRL_REG2, 2);
    if (ok) do {
        ok = spi_read_reg(RCV_CTRL_REG2, &val) && (val & 2) == 0;
    } while (!ok && tries-- && usdelay(10));
    return ok;
}

// Non-static so winc_mesh.c can use it
bool hif_put(uint16_t gop, void *dp1, int dlen1, void *dp2, int dlen2, int oset) {
    uint32_t addr, a, dlen = HIF_HDR_SIZE + (dlen2 ? oset + dlen2 : dlen1);
    uint8_t gid = (uint8_t)(gop >> 8), op = (uint8_t)gop;
    uint8_t hdr[8] = {gid, op & 0x7f, (uint8_t)dlen, (uint8_t)(dlen >> 8)};
    bool ok;

    ok = hif_start(gid, op, dlen);
    ok = ok && spi_read_reg(RCV_CTRL_REG4, &addr);
    ok = ok && spi_write_data(addr, hdr, sizeof(hdr));
    a = addr + HIF_HDR_SIZE;
    ok = ok && spi_write_data(a, dp1, dlen1);
    if (dp2 && dlen2)
        ok = ok && spi_write_data(a + oset, dp2, dlen2);
    ok = ok && spi_write_reg(RCV_CTRL_REG3, addr << 2 | 2);
    if (g_ctx.verbose > 1) {
        printf("Send gid=%u op=%u len=%u,%u\n", gid, op, dlen1, dlen2);
        dump_hex(dp1, dlen1, 16, "  ");
        if (dp2)
            dump_hex(dp2, dlen2, 16, "  ");
    }
    return ok;
}

static int hif_hdr_get(uint32_t addr, HIF_HDR *hp) {
    return spi_read_data(addr, (uint8_t *)hp, sizeof(HIF_HDR));
}

static int hif_get(uint32_t addr, void *buff, int len) {
    return spi_read_data(addr, (uint8_t *)buff, len) ? len : 0;
}

static bool hif_rx_done(void) {
    uint32_t val;
    return (spi_read_reg(RCV_CTRL_REG0, &val) &&
            spi_write_reg(RCV_CTRL_REG0, val | 2));
}

static bool join_net(char *ssid, char *pass) {
#if NEW_JOIN
    CONN_HDR ch = {pass ? 0x98 : 0x2c, CRED_STORE, ANY_CHAN, strlen(ssid), "",
                   pass ? AUTH_PSK : AUTH_OPEN, {0, 0, 0}};
    PSK_DATA pd;

    strcpy(ch.ssid, ssid);
    if (pass) {
        memset(&pd, 0, sizeof(PSK_DATA));
        strcpy(pd.phrase, pass);
        pd.len = strlen(pass);
        return hif_put(GOP_CONN_REQ_NEW | REQ_DATA, &ch, sizeof(CONN_HDR),
                      &pd, sizeof(PSK_DATA), sizeof(CONN_HDR));
    }
    return hif_put(GOP_CONN_REQ_NEW, &ch, sizeof(CONN_HDR), 0, 0, 0);
#else
    OLD_CONN_HDR och = {"", pass ? AUTH_PSK : AUTH_OPEN, {0, 0}, ANY_CHAN, "", 1, {0, 0}};

    strcpy(och.ssid, ssid);
    strcpy(och.psk, pass ? pass : "");
    return hif_put(GOP_CONN_REQ_OLD, &och, sizeof(OLD_CONN_HDR), 0, 0, 0);
#endif
}

// Socket Functions
static char *sock_err_str(int err) {
    err = err < 0 ? -err : err;
    return (err < sizeof(sock_errs) / sizeof(char *) ? sock_errs[err] : "");
}

// Forward declarations
static bool put_sock_bind(uint8_t sock, uint16_t port);
static void sock_state(uint8_t sock, int news);

// Non-static so winc_mesh.c can use it
int open_sock_server(int portnum, bool tcp, SOCK_HANDLER handler) {
    int sock, smin = tcp ? MIN_TCP_SOCK : MIN_UDP_SOCK, smax = tcp ? MAX_TCP_SOCK : MAX_UDP_SOCK;
    static uint16_t session = 1;

    for (sock = smin; sock < smax; sock++) {
        if (!g_ctx.sockets[sock].state) {
            g_ctx.sockets[sock].localport = portnum;
            g_ctx.sockets[sock].session = session++;
            g_ctx.sockets[sock].handler = handler;
            sock_state(sock, STATE_BINDING);

            // If network is already ready, bind immediately
            if (g_ctx.connection_state.dhcp_done) {
                printf("[SOCKET] Network already ready, binding socket %d immediately\n", sock);
                put_sock_bind(sock, portnum);
            }

            return sock;
        }
    }
    return -1;
}

static void sock_state(uint8_t sock, int news) {
    if (sock < MAX_SOCKETS)
        g_ctx.sockets[sock].state = news;
}

static bool put_sock_bind(uint8_t sock, uint16_t port) {
    SOCKET *sp = &g_ctx.sockets[sock];
    BIND_CMD bc = {
        .saddr = {.family = IP_FAMILY, .port = swap16(port), .ip = 0},  // INADDR_ANY (0.0.0.0)
        .sock = sock, .x = 0, .session = g_ctx.sockets[sock].session};

    memcpy(&sp->addr, &bc.saddr, sizeof(SOCK_ADDR));

    if (g_ctx.verbose)
        printf("[BIND] Binding socket %d to port %d (IP=0.0.0.0 INADDR_ANY)\n", sock, port);

    bool result = hif_put(GOP_BIND, &bc, sizeof(bc), 0, 0, 0);

    if (!result && g_ctx.verbose)
        printf("[BIND] ERROR: Failed to send bind command for socket %d\n", sock);

    return result;
}

static bool put_sock_listen(uint8_t sock) {
    LISTEN_CMD lc = {sock, 0, g_ctx.sockets[sock].session};
    return hif_put(GOP_LISTEN, &lc, sizeof(lc), 0, 0, 0);
}

static bool put_sock_recv(uint8_t sock) {
    RECV_CMD rc = {-1, sock, 0, g_ctx.sockets[sock].session};
    return hif_put(GOP_RECV, &rc, sizeof(rc), 0, 0, 0);
}

static bool put_sock_recvfrom(uint8_t sock) {
    RECVFROM_CMD rc = {-1, sock, 0, g_ctx.sockets[sock].session};
    return hif_put(GOP_RECVFROM, &rc, sizeof(rc), 0, 0, 0);
}

static bool put_sock_send(uint8_t sock, void *data, int len) {
    SOCKET *sp = &g_ctx.sockets[sock];
    SENDTO_CMD sc = {
        .saddr = {sp->addr.family, sp->addr.port, sp->addr.ip},
        .sock = sock, .len = len, .x = 0, .session = sp->session, .x2 = 0};
    return hif_put(GOP_SEND | REQ_DATA, &sc, sizeof(sc), data, len, TCP_DATA_OSET);
}

bool put_sock_sendto(uint8_t sock, void *data, int len) {
    SOCKET *sp = &g_ctx.sockets[sock];

    // For mesh UDP broadcasts, use broadcast address and local port if not set
    uint32_t dest_ip = sp->addr.ip;
    uint16_t dest_port = sp->addr.port;

    if (dest_ip == 0) {
        dest_ip = 0xFFFFFFFF;  // 255.255.255.255 (broadcast)
    }

    if (dest_port == 0) {
        dest_port = sp->localport;  // Send to same port we're listening on
    }

    // Debug output
    if (g_ctx.verbose) {
        printf("[SENDTO] sock=%d, state=%d, family=%d, port=%d, IP=%u.%u.%u.%u, len=%d\n",
               sock, sp->state, sp->addr.family, dest_port,
               (dest_ip >> 0) & 0xFF, (dest_ip >> 8) & 0xFF,
               (dest_ip >> 16) & 0xFF, (dest_ip >> 24) & 0xFF, len);
    }

    SENDTO_CMD sc = {
        .saddr = {sp->addr.family, swap16(dest_port), dest_ip},  // FIXED: byte swap port
        .sock = sock, .len = len, .x = 0, .session = sp->session, .x2 = 0};

    bool result = hif_put(GOP_SENDTO | REQ_DATA, &sc, sizeof(sc), data, len, UDP_DATA_OSET);

    if (!result && g_ctx.verbose) {
        printf("[SENDTO] ERROR: hif_put failed!\n");
    }

    return result;
}

static bool put_sock_close(uint8_t sock) {
    CLOSE_CMD cc = {sock, 0, g_ctx.sockets[sock].session};
    bool ok = hif_put(GOP_CLOSE, &cc, sizeof(cc), 0, 0, 0);
    memset(&g_ctx.sockets[sock], 0, sizeof(SOCKET));
    return ok;
}

bool get_sock_data(uint8_t sock, void *data, int len) {
    SOCKET *sp = &g_ctx.sockets[sock];
    bool ok = 0;
    if (len > 0)
        ok = hif_get(sp->hif_data_addr, data, len);
    return ok;
}

static void tcp_echo_handler(uint8_t sock, int rxlen) {
    printf("TCP Rx socket %u len %d %s\n", sock, rxlen,
           rxlen <= 0 ? sock_err_str(rxlen) : "");
    if (rxlen < 0)
        put_sock_close(sock);
    else if (rxlen > 0 && get_sock_data(sock, g_ctx.databuf, rxlen)) {
        if (g_ctx.verbose > 1)
            dump_hex(g_ctx.databuf, rxlen, 16, "  ");
        put_sock_send(sock, g_ctx.databuf, rxlen);
    }
}

static void udp_echo_handler(uint8_t sock, int rxlen) {
    printf("UDP Rx socket %u len %d %s\n", sock, rxlen,
           rxlen <= 0 ? sock_err_str(rxlen) : "");
    if (rxlen > 0 && get_sock_data(sock, g_ctx.databuf, rxlen)) {
        if (g_ctx.verbose > 1)
            dump_hex(g_ctx.databuf, rxlen, 16, "  ");
        put_sock_sendto(sock, g_ctx.databuf, rxlen);
    }
}

// Check for socket actions
static void check_sock(uint16_t gop, RESP_MSG *rmp) {
    SOCKET *sp;
    uint8_t sock, sock2;

    if (gop == GOP_DHCP_CONF || gop == GOP_AP_ENABLE || gop == GOP_DHCP_CONF_AP) {
        // Bind all pending sockets when DHCP completes (client) or AP enables (AP mode)
        const char *event_name = (gop == GOP_DHCP_CONF) ? "GOP_DHCP_CONF" :
                                  (gop == GOP_AP_ENABLE) ? "GOP_AP_ENABLE" : "GOP_DHCP_CONF_AP";
        printf("[%s] Checking for sockets to bind...\n", event_name);
        int bound_count = 0;
        for (sock = MIN_SOCKET; sock < MAX_SOCKETS; sock++) {
            sp = &g_ctx.sockets[sock];
            if (sp->state == STATE_BINDING) {
                printf("[EVENT] Binding socket %d (port=%d)\n", sock, sp->localport);
                put_sock_bind(sock, sp->localport);
                bound_count++;
            }
        }
        if (bound_count == 0) {
            printf("[EVENT] No sockets in STATE_BINDING to bind\n");
        } else {
            printf("[EVENT] Sent bind command for %d socket(s)\n", bound_count);
        }
    }
    else if (gop == GOP_BIND && (sock = rmp->bind.sock) < MAX_SOCKETS &&
             g_ctx.sockets[sock].state == STATE_BINDING) {
        printf("[GOP_BIND] Socket %d transitioning to STATE_BOUND\n", sock);
        sock_state(sock, STATE_BOUND);
        if (sock < MIN_UDP_SOCK) {
            printf("[GOP_BIND] TCP socket %d: sending LISTEN\n", sock);
            put_sock_listen(sock);
        } else {
            printf("[GOP_BIND] UDP socket %d: sending RECVFROM\n", sock);
            put_sock_recvfrom(sock);
        }
    }
    else if (gop == GOP_BIND) {
        // GOP_BIND received but socket state mismatch
        if (rmp->bind.sock < MAX_SOCKETS) {
            printf("[GOP_BIND] WARNING: Socket %d state=%d (expected STATE_BINDING=%d)\n",
                   rmp->bind.sock, g_ctx.sockets[rmp->bind.sock].state, STATE_BINDING);
        } else {
            printf("[GOP_BIND] ERROR: Invalid socket number %d (max=%d)\n",
                   rmp->bind.sock, MAX_SOCKETS);
        }
    }
    else if (gop == GOP_RECVFROM && (sock = rmp->recv.sock) < MAX_SOCKETS &&
             (sp = &g_ctx.sockets[sock])->state == STATE_BOUND) {
        memcpy(&sp->addr, &rmp->recv.addr, sizeof(SOCK_ADDR));
        if (sp->handler)
            sp->handler(sock, rmp->recv.dlen);
        put_sock_recvfrom(sock);
    }
    else if (gop == GOP_ACCEPT &&
             (sock = rmp->accept.listen_sock) < MAX_SOCKETS &&
             (sock2 = rmp->accept.conn_sock) < MAX_SOCKETS &&
             g_ctx.sockets[sock].state == STATE_BOUND) {
        memcpy(&g_ctx.sockets[sock2].addr, &rmp->recv.addr, sizeof(SOCK_ADDR));
        g_ctx.sockets[sock2].handler = g_ctx.sockets[sock].handler;
        sock_state(sock2, STATE_CONNECTED);
        put_sock_recv(sock2);
    }
    else if (gop == GOP_RECV && (sock = rmp->recv.sock) < MAX_SOCKETS &&
            (sp = &g_ctx.sockets[sock])->state == STATE_CONNECTED) {
        if (sp->handler)
            sp->handler(sock, rmp->recv.dlen);
        if (rmp->recv.dlen > 0)
            put_sock_recv(sock);
    }
}

// Interrupt handler
void interrupt_handler(void) {
    bool ok = 1;
    int hlen;
    uint16_t gop;
    uint32_t val, size, addr = 0;
    HIF_HDR hh;
    RESP_MSG *rmp = &g_ctx.resp_msg;
    char temps[50] = "";

    if (g_ctx.verbose > 1)
        printf("Interrupt\n");
    
    ok = spi_read_reg(RCV_CTRL_REG0, &val) &&
         (val & 1) && (size = (val >> 2) & 0xfff) != 0;
    ok = ok && spi_write_reg(RCV_CTRL_REG0, val & ~1);
    ok = ok && spi_read_reg(RCV_CTRL_REG1, &addr) && addr;

    // Read HIF header
    ok = ok && hif_get(addr, &hh, sizeof(hh));
    gop = GIDOP((uint16_t)hh.gid, hh.op);
    hlen = MIN((hh.len - HIF_HDR_SIZE), sizeof(RESP_MSG));

    // Read response message
    ok = ok && hlen > 0 && hif_get(addr + HIF_HDR_SIZE, rmp, hlen);

    // Act on response
    if (gop == GOP_STATE_CHANGE && ok) {
        sprintf(temps, rmp->val == 0 ? "disconnected" : rmp->val == 1 ? "connected" : "fail");

        // Track connection state
        if (rmp->val == 1) {
            g_ctx.connection_state.connected = true;
            printf("[STATE] WiFi connected!\n");
        } else if (rmp->val == 0) {
            g_ctx.connection_state.connected = false;
            g_ctx.connection_state.dhcp_done = false;
            printf("[STATE] WiFi disconnected!\n");
        }
    }
    else if (gop == GOP_DHCP_CONF && ok) {
        sprintf(temps, "%u.%u.%u.%u gate %u.%u.%u.%u", IP_BYTES(rmp->dhcp.self), IP_BYTES(rmp->dhcp.gate));

        // Track DHCP state
        g_ctx.connection_state.dhcp_done = true;
        g_ctx.connection_state.my_ip = rmp->dhcp.self;
        printf("[STATE] DHCP complete! IP: %u.%u.%u.%u\n", IP_BYTES(rmp->dhcp.self));
    }
    else if (gop == GOP_AP_ENABLE && ok) {
        sprintf(temps, "AP mode enabled");
        printf("[STATE] AP mode enabled!\n");

        // Set AP mode flags - AP is "connected" immediately
        g_ctx.connection_state.connected = true;    // AP is always "connected"
        g_ctx.connection_state.ap_mode = true;
        g_ctx.connection_state.dhcp_done = true;    // AP is "ready" like DHCP complete

        // AP has static IP (typically 192.168.1.1), set a placeholder
        g_ctx.connection_state.my_ip = 0xC0A80101;  // 192.168.1.1
    }
    else if (gop == GOP_DHCP_CONF_AP && ok) {
        sprintf(temps, "AP DHCP server configured");
        printf("[STATE] AP DHCP server ready!\n");

        // Ensure flags are set (should already be set by GOP_AP_ENABLE)
        g_ctx.connection_state.connected = true;
        g_ctx.connection_state.ap_mode = true;
        g_ctx.connection_state.dhcp_done = true;
    }
    else if (gop == GOP_AP_ASSOC_INFO) {
        sprintf(temps, "Client association info");
        printf("[STATE] Client %s AP\n", ok ? "associated with" : "disconnected from");
    }
    else if (gop == GOP_BIND && ok)
        sprintf(temps, "0x%X", rmp->val);
    else if (gop == GOP_ACCEPT && ok)
        sprintf(temps, "%u.%u.%u.%u:%u sock %u,%u",
            IP_BYTES(rmp->accept.addr.ip), rmp->accept.addr.port,
            rmp->accept.listen_sock, rmp->accept.conn_sock);
    else if (gop == GOP_RECVFROM && ok) {
        sprintf(temps, "%u.%u.%u.%u:%u sock %d dlen %d",
                IP_BYTES(rmp->recv.addr.ip), rmp->recv.addr.port, rmp->recv.sock, rmp->recv.dlen);
        if (rmp->recv.sock < MAX_SOCKETS)
            g_ctx.sockets[rmp->recv.sock].hif_data_addr = addr + HIF_HDR_SIZE + rmp->recv.oset;
    }
    else if (gop == GOP_RECV && ok) {
        sprintf(temps, "sock %d dlen %d", rmp->recv.sock, rmp->recv.dlen);
        if (rmp->recv.sock < MAX_SOCKETS)
            g_ctx.sockets[rmp->recv.sock].hif_data_addr = addr + HIF_HDR_SIZE + rmp->recv.oset;
    }
    
    if (g_ctx.verbose) {
        printf("Interrupt gid %u op %u len %u %s %s\n",
               hh.gid, hh.op, hh.len, op_str(hh.gid, hh.op), temps);
    }
    
    check_sock(gop, rmp);
    ok = ok && hif_rx_done();
    
    if (g_ctx.verbose > 1)
        printf("Interrupt complete %s\n", ok ? "OK" : "error");
}

// ====== AP MODE FUNCTIONS ======

// Start AP mode (SoftAP)
bool winc_start_ap(const char *ssid, const char *password, uint8_t channel) {
    AP_CONFIG ap_cfg;

    printf("Starting AP mode: %s (channel %u)\n", ssid, channel);

    memset(&ap_cfg, 0, sizeof(ap_cfg));
    strncpy(ap_cfg.ssid, ssid, 32);
    ap_cfg.channel = channel;

    if (password && strlen(password) > 0) {
        ap_cfg.sec_type = AUTH_PSK;  // WPA2
        ap_cfg.key_len = strlen(password);
        strncpy(ap_cfg.key, password, 63);
    } else {
        ap_cfg.sec_type = AUTH_OPEN;  // Open network
    }

    ap_cfg.ssid_hide = 0;      // Broadcast SSID
    ap_cfg.dhcp_enable = 1;    // Enable DHCP server

    bool ok = hif_put(GOP_AP_ENABLE, &ap_cfg, sizeof(ap_cfg), 0, 0, 0);

    if (!ok) {
        printf("ERROR: Failed to start AP mode\n");
        return false;
    }

    printf("AP mode command sent, waiting for ready...\n");

    // Wait for AP to start
    uint32_t start = to_ms_since_boot(get_absolute_time());

    while ((to_ms_since_boot(get_absolute_time()) - start) < 10000) {
        if (gpio_get(g_ctx.pins.irq) == 0) {
            interrupt_handler();
        }
        sleep_ms(100);
    }

    printf("AP mode active!\n");
    return true;
}

// Stop AP mode
bool winc_stop_ap(void) {
    printf("Stopping AP mode\n");
    return hif_put(GOP_AP_DISABLE, NULL, 0, 0, 0, 0);
}

// Connect to AP (station mode)
bool winc_connect_sta(const char *ssid, const char *password) {
    printf("Connecting to AP: %s\n", ssid);

    if (!join_net((char*)ssid, (char*)password)) {
        printf("ERROR: Failed to start connection\n");
        return false;
    }

    printf("Connection initiated, waiting for DHCP...\n");

    uint32_t start = to_ms_since_boot(get_absolute_time());

    while ((to_ms_since_boot(get_absolute_time()) - start) < 15000) {
        if (gpio_get(g_ctx.pins.irq) == 0) {
            interrupt_handler();
        }

        if (g_ctx.connection_state.dhcp_done) {
            printf("Connected and got IP!\n");

            // Brief stabilization delay (no interrupt polling to avoid state machine issues)
            sleep_ms(1000);

            // Final connection verification
            if (!g_ctx.connection_state.connected) {
                printf("ERROR: Connection lost after DHCP\n");
                return false;
            }

            printf("Connection ready!\n");
            return true;
        }
        sleep_ms(100);
    }

    printf("ERROR: Connection/DHCP timeout\n");
    return false;
}

// ====== PUBLIC API IMPLEMENTATION ======
bool winc_init(uint8_t node_id, const char *node_name) {
    memset(&g_ctx, 0, sizeof(g_ctx));

    // Set pins from defines
    g_ctx.pins.sck = WINC_PIN_SCK;
    g_ctx.pins.mosi = WINC_PIN_MOSI;
    g_ctx.pins.miso = WINC_PIN_MISO;
    g_ctx.pins.cs = WINC_PIN_CS;
    g_ctx.pins.wake = WINC_PIN_WAKE;
    g_ctx.pins.reset = WINC_PIN_RESET;
    g_ctx.pins.irq = WINC_PIN_IRQ;

    // Initialize SPI
    spi_init(spi0, WINC_SPI_SPEED);
    gpio_set_function(g_ctx.pins.miso, GPIO_FUNC_SPI);
    gpio_set_function(g_ctx.pins.sck, GPIO_FUNC_SPI);
    gpio_set_function(g_ctx.pins.mosi, GPIO_FUNC_SPI);
    
    // CS pin
    gpio_init(g_ctx.pins.cs);
    gpio_set_dir(g_ctx.pins.cs, GPIO_OUT);
    gpio_put(g_ctx.pins.cs, 1);
    
    // Wake pin
    gpio_init(g_ctx.pins.wake);
    gpio_set_dir(g_ctx.pins.wake, GPIO_OUT);
    gpio_put(g_ctx.pins.wake, 0);
    
    // Reset pin
    gpio_init(g_ctx.pins.reset);
    gpio_set_dir(g_ctx.pins.reset, GPIO_OUT);
    gpio_put(g_ctx.pins.reset, 1);
    
    // IRQ pin
    gpio_init(g_ctx.pins.irq);
    gpio_set_dir(g_ctx.pins.irq, GPIO_IN);
    gpio_pull_up(g_ctx.pins.irq);
    
    // Reset chip
    gpio_put(g_ctx.pins.reset, 0);
    msdelay(10);
    gpio_put(g_ctx.pins.reset, 1);
    msdelay(10);

    // Initialize chip
    printf("Disabling CRC and initializing WINC chip...\n");
    disable_crc();
    if (!chip_init()) {
        printf("ERROR: Failed to initialize WINC chip\n");
        return false;
    }
    printf("WINC chip initialized successfully\n");

    chip_get_info();
    printf("Firmware version: %d.%d.%d\n", g_ctx.fw_major, g_ctx.fw_minor, g_ctx.fw_patch);

    // Initialize mesh networking (will configure AP or Client mode based on node_id)
    printf("\nStarting mesh initialization...\n");
    bool mesh_result = winc_mesh_init(node_id, node_name);
    printf("Mesh init returned: %s\n", mesh_result ? "SUCCESS" : "FAILURE");

    return mesh_result;
}

void winc_poll(void) {
    // Check IRQ
    if (gpio_get(g_ctx.pins.irq) == 0) {
        interrupt_handler();
    }

    // Process mesh
    winc_mesh_process();
}

void winc_mesh_set_callback(void (*callback)(uint8_t src_node, uint8_t *data, uint16_t len)) {
    g_ctx.mesh.data_callback = callback;
}

// winc_mesh_send is implemented in winc_mesh.c

void winc_mesh_print_routes(void) {
    printf("Mesh Routing Table (Node %u - \"%s\"):\n", g_ctx.mesh.my_node_id, g_ctx.mesh.my_name);
    for (int i = 0; i < g_ctx.mesh.route_count; i++) {
        if (g_ctx.mesh.routes[i].active) {
            printf("  Node %u: %u hop%s",
                   g_ctx.mesh.routes[i].node_id,
                   g_ctx.mesh.routes[i].hop_count,
                   g_ctx.mesh.routes[i].hop_count == 1 ? " (direct)" : "s");
            if (g_ctx.mesh.routes[i].hop_count > 1) {
                printf(" via node %u", g_ctx.mesh.routes[i].next_hop);
            }
            printf("\n");
        }
    }
}

uint8_t winc_mesh_get_node_count(void) {
    uint8_t count = 0;
    for (int i = 0; i < g_ctx.mesh.route_count; i++) {
        if (g_ctx.mesh.routes[i].active)
            count++;
    }
    return count;
}

void winc_set_verbose(int level) {
    g_ctx.verbose = level;
}

void winc_get_firmware_version(uint8_t *major, uint8_t *minor, uint8_t *patch) {
    if (major) *major = g_ctx.fw_major;
    if (minor) *minor = g_ctx.fw_minor;
    if (patch) *patch = g_ctx.fw_patch;
}

void winc_get_mac(uint8_t mac[6]) {
    if (mac) {
        memcpy(mac, g_ctx.mac, 6);
    }
}

uint8_t winc_get_node_id(void) {
    return g_ctx.mesh.my_node_id;
}

const char* winc_get_node_name(void) {
    return g_ctx.mesh.my_name;
}

// Note: winc_mesh_init and winc_mesh_process are implemented in winc_mesh.c