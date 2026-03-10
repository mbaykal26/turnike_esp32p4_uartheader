#pragma once
/*
 * eth_ip101.h – Ethernet via IP101 PHY + internal ESP32-P4 EMAC
 * MDC=GPIO31, MDIO=GPIO52, RST=GPIO51, PHY_ADDR=1
 */
#include <stdbool.h>
#include "esp_err.h"

/**
 * Initialise the Ethernet stack.
 * Blocks until link comes up (max ~30 s) or returns on timeout.
 * Registers event handlers for link/IP changes.
 */
esp_err_t eth_init(void);

/** Return true if Ethernet link is up and an IP address has been obtained. */
bool eth_is_connected(void);

/**
 * Fill ip_str with the current IPv4 address string (e.g. "192.168.1.42").
 * buf_size should be at least 16 bytes.
 * Returns false if not connected.
 */
bool eth_get_ip_str(char *ip_str, size_t buf_size);

/** Returns MAC address as 6-byte array. */
void eth_get_mac(uint8_t mac[6]);
