// ATWINC1500/1510 WiFi module P2P/mesh implementation for the Pico 2W
//
// P2P (Wi-Fi Direct) and mesh networking support
// Based on ATWINC1500 P2P mode application note
//
// Created by Niles Roxas (2025)

#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include "pico/stdlib.h"
#include "winc_wifi.h"
#include "winc_sock.h"
#include "winc_p2p.h"

// Global state variables
static bool p2p_enabled = false;
static bool mesh_enabled = false;
static uint8_t p2p_mode = P2P_MODE_IDLE;
static MESH_ROUTING_TABLE routing_table;
static uint16_t mesh_seq_num = 0;
static char local_node_name[16];
static uint32_t last_beacon_time = 0;

extern int verbose, spi_fd;

// Enable P2P mode on ATWINC1500
bool p2p_enable(int fd, uint8_t channel)
{
    P2P_ENABLE_CMD cmd;
    bool ok;

    if (verbose)
        printf("Enabling P2P mode on channel %u\n", channel);

    memset(&cmd, 0, sizeof(cmd));
    cmd.channel = channel;

    ok = hif_put(fd, GOP_P2P_ENABLE, &cmd, sizeof(cmd), 0, 0, 0);

    if (ok)
    {
        p2p_enabled = true;
        p2p_mode = P2P_MODE_IDLE;
        if (verbose)
            printf("P2P mode enabled\n");
    }
    else
    {
        printf("Failed to enable P2P mode\n");
    }

    return ok;
}

// Disable P2P mode
bool p2p_disable(int fd)
{
    bool ok;

    if (verbose)
        printf("Disabling P2P mode\n");

    ok = hif_put(fd, GOP_P2P_DISABLE, NULL, 0, 0, 0, 0);

    if (ok)
    {
        p2p_enabled = false;
        p2p_mode = P2P_MODE_IDLE;
        if (verbose)
            printf("P2P mode disabled\n");
    }

    return ok;
}

// Start P2P listen mode
bool p2p_start_listen(int fd, uint8_t channel)
{
    if (!p2p_enabled)
    {
        printf("P2P mode not enabled\n");
        return false;
    }

    if (verbose)
        printf("Starting P2P listen on channel %u\n", channel);

    p2p_mode = P2P_MODE_LISTEN;

    // The ATWINC1500 automatically listens when P2P is enabled
    // This function is mainly for state tracking
    return true;
}

// Start P2P device search/discovery
bool p2p_start_search(int fd)
{
    if (!p2p_enabled)
    {
        printf("P2P mode not enabled\n");
        return false;
    }

    if (verbose)
        printf("Starting P2P device search\n");

    p2p_mode = P2P_MODE_SEARCH;

    // Search is automatic in P2P mode
    return true;
}

// Connect to a P2P peer using WPS Push Button Configuration
bool p2p_connect_wps_pbc(int fd)
{
    WPS_REQ req;
    bool ok;

    if (!p2p_enabled)
    {
        printf("P2P mode not enabled\n");
        return false;
    }

    if (verbose)
        printf("Starting P2P connection with WPS-PBC\n");

    memset(&req, 0, sizeof(req));
    req.trigger_type = WPS_PBC;

    ok = hif_put(fd, GOP_WPS_REQ, &req, sizeof(req), 0, 0, 0);

    if (ok && verbose)
        printf("WPS-PBC connection request sent\n");

    return ok;
}

// Connect to a P2P peer using WPS PIN
bool p2p_connect_wps_pin(int fd, uint8_t *pin)
{
    WPS_REQ req;
    bool ok;

    if (!p2p_enabled)
    {
        printf("P2P mode not enabled\n");
        return false;
    }

    if (verbose)
        printf("Starting P2P connection with WPS-PIN\n");

    memset(&req, 0, sizeof(req));
    req.trigger_type = WPS_PIN;
    memcpy(req.pin, pin, 8);

    ok = hif_put(fd, GOP_WPS_REQ, &req, sizeof(req), 0, 0, 0);

    if (ok && verbose)
        printf("WPS-PIN connection request sent\n");

    return ok;
}

// Initialize mesh networking
bool mesh_init(int fd, uint8_t node_id, char *node_name)
{
    if (verbose)
        printf("Initializing mesh network, node ID: %u, name: %s\n", node_id, node_name);

    memset(&routing_table, 0, sizeof(routing_table));
    routing_table.local_node_id = node_id;
    routing_table.node_count = 0;

    strncpy(local_node_name, node_name, sizeof(local_node_name) - 1);
    local_node_name[sizeof(local_node_name) - 1] = '\0';

    mesh_seq_num = 0;
    last_beacon_time = 0;

    return true;
}

// Enable mesh networking (requires P2P to be enabled first)
bool mesh_enable(int fd)
{
    if (!p2p_enabled)
    {
        printf("P2P mode must be enabled before mesh networking\n");
        return false;
    }

    if (verbose)
        printf("Enabling mesh networking\n");

    mesh_enabled = true;
    last_beacon_time = time_us_32() / 1000;

    // Start listening for P2P connections
    p2p_start_listen(fd, P2P_LISTEN_CHAN);

    return true;
}

// Disable mesh networking
bool mesh_disable(int fd)
{
    if (verbose)
        printf("Disabling mesh networking\n");

    mesh_enabled = false;

    return true;
}

// Send mesh beacon to announce presence
bool mesh_send_beacon(int fd)
{
    MESH_BEACON beacon;
    uint8_t i, neighbor_idx = 0;

    if (!mesh_enabled)
        return false;

    memset(&beacon, 0, sizeof(beacon));

    // Fill beacon header
    beacon.hdr.msg_type = MESH_MSG_BEACON;
    beacon.hdr.src_node = routing_table.local_node_id;
    beacon.hdr.dst_node = 0xFF; // Broadcast
    beacon.hdr.hop_count = 0;
    beacon.hdr.seq_num = mesh_seq_num++;
    beacon.hdr.payload_len = sizeof(MESH_BEACON) - sizeof(MESH_PKT_HDR);

    // Fill beacon data
    beacon.node_id = routing_table.local_node_id;
    strncpy((char *)beacon.node_name, local_node_name, sizeof(beacon.node_name) - 1);

    // Add active neighbors to beacon
    for (i = 0; i < routing_table.node_count && neighbor_idx < MESH_MAX_NODES; i++)
    {
        if (routing_table.nodes[i].is_active && routing_table.nodes[i].hop_count == 1)
        {
            beacon.neighbors[neighbor_idx++] = routing_table.nodes[i].node_id;
        }
    }
    beacon.neighbor_count = neighbor_idx;

    if (verbose > 1)
        printf("Sending mesh beacon, neighbors: %u\n", beacon.neighbor_count);

    // Send beacon via P2P broadcast
    // Note: This uses UDP broadcast on the P2P network
    // You'll need to set up a UDP socket for mesh communication

    last_beacon_time = time_us_32() / 1000;

    return true;
}

// Send data through mesh network
bool mesh_send_data(int fd, uint8_t dst_node, uint8_t *data, uint16_t len)
{
    MESH_PKT_HDR hdr;
    int next_hop;

    if (!mesh_enabled)
    {
        printf("Mesh networking not enabled\n");
        return false;
    }

    // Find route to destination
    next_hop = mesh_find_route(dst_node);

    if (next_hop < 0)
    {
        printf("No route to destination node %u\n", dst_node);
        return false;
    }

    // Build packet header
    hdr.msg_type = MESH_MSG_DATA;
    hdr.src_node = routing_table.local_node_id;
    hdr.dst_node = dst_node;
    hdr.hop_count = 0;
    hdr.seq_num = mesh_seq_num++;
    hdr.payload_len = len;

    if (verbose)
        printf("Sending mesh data to node %u via next hop %d\n", dst_node, next_hop);

    // Send packet to next hop
    // This would use the socket interface to send to the next hop node

    return true;
}

// Route a packet through the mesh network
bool mesh_route_packet(int fd, MESH_PKT_HDR *pkt, uint8_t *data)
{
    int next_hop;

    // Check if packet is for this node
    if (pkt->dst_node == routing_table.local_node_id)
    {
        // Handle packet locally
        mesh_data_handler(fd, data, pkt->payload_len);
        return true;
    }

    // Check hop count
    if (pkt->hop_count >= MESH_MAX_HOPS)
    {
        if (verbose)
            printf("Packet exceeded max hops, dropping\n");
        return false;
    }

    // Find next hop
    next_hop = mesh_find_route(pkt->dst_node);

    if (next_hop < 0)
    {
        if (verbose)
            printf("No route to node %u, dropping packet\n", pkt->dst_node);
        return false;
    }

    // Increment hop count and forward
    pkt->hop_count++;

    if (verbose > 1)
        printf("Routing packet to node %u via hop %d\n", pkt->dst_node, next_hop);

    // Forward packet
    // This would use the socket interface

    return true;
}

// Update routing table from received beacon
void mesh_update_routing_table(MESH_BEACON *beacon)
{
    uint8_t i;
    MESH_NODE *node = NULL;
    uint32_t current_time = time_us_32() / 1000;

    // Find existing node or add new one
    for (i = 0; i < routing_table.node_count; i++)
    {
        if (routing_table.nodes[i].node_id == beacon->node_id)
        {
            node = &routing_table.nodes[i];
            break;
        }
    }

    // Add new node if not found and space available
    if (!node && routing_table.node_count < MESH_MAX_NODES)
    {
        node = &routing_table.nodes[routing_table.node_count++];
        node->node_id = beacon->node_id;
    }

    if (node)
    {
        // Update node information
        node->hop_count = beacon->hdr.hop_count + 1;
        node->last_update = current_time;
        node->is_active = true;

        // If this is a direct neighbor (hop_count == 1), it's our next hop
        if (node->hop_count == 1)
        {
            node->next_hop = beacon->node_id;
        }

        if (verbose > 1)
            printf("Updated routing table: node %u, hops %u\n",
                   node->node_id, node->hop_count);
    }
}

// Find route to destination node
int mesh_find_route(uint8_t dst_node)
{
    uint8_t i;
    MESH_NODE *best_node = NULL;
    uint8_t min_hops = 255;

    // Find node with minimum hop count
    for (i = 0; i < routing_table.node_count; i++)
    {
        if (routing_table.nodes[i].node_id == dst_node &&
            routing_table.nodes[i].is_active &&
            routing_table.nodes[i].hop_count < min_hops)
        {
            best_node = &routing_table.nodes[i];
            min_hops = routing_table.nodes[i].hop_count;
        }
    }

    if (best_node)
        return best_node->next_hop;

    return -1;
}

// Handle received mesh beacon
void mesh_beacon_handler(int fd)
{
    uint32_t current_time = time_us_32() / 1000;
    uint8_t i;

    if (!mesh_enabled)
        return;

    // Clean up stale routes
    for (i = 0; i < routing_table.node_count; i++)
    {
        if (routing_table.nodes[i].is_active &&
            (current_time - routing_table.nodes[i].last_update) > MESH_ROUTE_TIMEOUT)
        {
            routing_table.nodes[i].is_active = false;
            if (verbose)
                printf("Route to node %u timed out\n", routing_table.nodes[i].node_id);
        }
    }

    // Send periodic beacons
    if ((current_time - last_beacon_time) > MESH_BEACON_INTERVAL)
    {
        mesh_send_beacon(fd);
    }
}

// Handle received mesh data
void mesh_data_handler(int fd, uint8_t *data, uint16_t len)
{
    if (verbose)
    {
        printf("Received mesh data, length: %u\n", len);
        dump_hex(data, len, 16, "  ");
    }

    // Application-specific data handling would go here
}

// Print routing table for debugging
void mesh_print_routing_table(void)
{
    uint8_t i;

    printf("\n=== Mesh Routing Table ===\n");
    printf("Local Node ID: %u (%s)\n", routing_table.local_node_id, local_node_name);
    printf("Active Nodes: %u\n", routing_table.node_count);
    printf("Node ID  Hops  Next Hop  Active\n");
    printf("-------  ----  --------  ------\n");

    for (i = 0; i < routing_table.node_count; i++)
    {
        MESH_NODE *node = &routing_table.nodes[i];
        printf("   %3u    %2u      %3u      %s\n",
               node->node_id,
               node->hop_count,
               node->next_hop,
               node->is_active ? "Yes" : "No");
    }
    printf("========================\n\n");
}

// Check if P2P is enabled
bool is_p2p_enabled(void)
{
    return p2p_enabled;
}

// Check if mesh is enabled
bool is_mesh_enabled(void)
{
    return mesh_enabled;
}

// P2P peer found callback (called when a peer is discovered)
void p2p_peer_found_handler(P2P_PEER *peer)
{
    if (verbose)
    {
        printf("P2P peer found: %s, channel %u, RSSI %d\n",
               peer->device_name, peer->channel, peer->rssi);
        printf("  MAC: %02X:%02X:%02X:%02X:%02X:%02X\n",
               peer->mac_addr[0], peer->mac_addr[1], peer->mac_addr[2],
               peer->mac_addr[3], peer->mac_addr[4], peer->mac_addr[5]);
    }
}

// EOF
