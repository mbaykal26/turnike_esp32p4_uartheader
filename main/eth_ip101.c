/*
 * eth_ip101.c – Ethernet via IP101 PHY + ESP32-P4 internal EMAC
 *
 * Uses esp_eth + esp_netif stack.
 * Same GPIO assignments as the Arduino P4 project:
 *   MDC=GPIO31, MDIO=GPIO52, RST=GPIO51, PHY_ADDR=1
 */

#include "eth_ip101.h"
#include "config.h"

#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_eth.h"
#include "esp_eth_mac.h"
#include "esp_eth_phy.h"
#include "esp_netif.h"
#include "esp_event.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "lwip/ip4_addr.h"

static const char *TAG = "eth_ip101";

static EventGroupHandle_t s_eth_event_group;
#define ETH_CONNECTED_BIT    BIT0
#define ETH_GOT_IP_BIT       BIT1
#define ETH_DISCONNECTED_BIT BIT2

static volatile bool s_connected = false;
static char          s_ip_str[16] = { 0 };
static uint8_t       s_mac[6]     = { 0 };

// ─── Event handlers ──────────────────────────────────────────────

static void eth_event_handler(void *arg, esp_event_base_t event_base,
                              int32_t event_id, void *event_data)
{
    switch (event_id) {
    case ETHERNET_EVENT_CONNECTED:
        ESP_LOGI(TAG, "Ethernet link UP");
        xEventGroupSetBits(s_eth_event_group, ETH_CONNECTED_BIT);
        xEventGroupClearBits(s_eth_event_group, ETH_DISCONNECTED_BIT);
        break;

    case ETHERNET_EVENT_DISCONNECTED:
        ESP_LOGW(TAG, "Ethernet link DOWN");
        s_connected = false;
        memset(s_ip_str, 0, sizeof(s_ip_str));
        xEventGroupSetBits(s_eth_event_group, ETH_DISCONNECTED_BIT);
        xEventGroupClearBits(s_eth_event_group, ETH_CONNECTED_BIT | ETH_GOT_IP_BIT);
        break;

    case ETHERNET_EVENT_START:
        // Retrieve MAC once driver starts
        {
            esp_eth_handle_t *eth_handle = (esp_eth_handle_t *)arg;
            esp_eth_ioctl(*eth_handle, ETH_CMD_G_MAC_ADDR, s_mac);
            ESP_LOGI(TAG, "ETH started, MAC: %02X:%02X:%02X:%02X:%02X:%02X",
                     s_mac[0], s_mac[1], s_mac[2],
                     s_mac[3], s_mac[4], s_mac[5]);
        }
        break;

    default:
        break;
    }
}

static void got_ip_event_handler(void *arg, esp_event_base_t event_base,
                                 int32_t event_id, void *event_data)
{
    ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
    snprintf(s_ip_str, sizeof(s_ip_str), IPSTR, IP2STR(&event->ip_info.ip));
    s_connected = true;
    ESP_LOGI(TAG, "Got IP: %s  Mask: " IPSTR "  GW: " IPSTR,
             s_ip_str,
             IP2STR(&event->ip_info.netmask),
             IP2STR(&event->ip_info.gw));
    xEventGroupSetBits(s_eth_event_group, ETH_GOT_IP_BIT);
}

// ─── Public API ──────────────────────────────────────────────────

esp_err_t eth_init(void)
{
    ESP_LOGI(TAG, "Ethernet init: MDC=GPIO%d MDIO=GPIO%d RST=GPIO%d PHY_ADDR=%d",
             ETH_MDC_GPIO, ETH_MDIO_GPIO, ETH_PHY_RST_GPIO, ETH_PHY_ADDR);

    // NVS is required by some net stack internals
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        nvs_flash_erase();
        ret = nvs_flash_init();
    }
    if (ret != ESP_OK) return ret;

    // Event loop — ignore INVALID_STATE (already created by another component)
    ret = esp_event_loop_create_default();
    if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) return ret;
    // netif init (idempotent in ESP-IDF 5.x)
    esp_netif_init();

    esp_netif_config_t netif_cfg = ESP_NETIF_DEFAULT_ETH();
    esp_netif_t *eth_netif = esp_netif_new(&netif_cfg);
    if (!eth_netif) return ESP_FAIL;

    // Set hostname
    esp_netif_set_hostname(eth_netif, DEVICE_HOSTNAME);

    s_eth_event_group = xEventGroupCreate();

    // ── MAC ───────────────────────────────────────────────────────
    eth_mac_config_t mac_config = ETH_MAC_DEFAULT_CONFIG();

    eth_esp32_emac_config_t emac_config = ETH_ESP32_EMAC_DEFAULT_CONFIG();
    emac_config.smi_gpio.mdc_num  = ETH_MDC_GPIO;
    emac_config.smi_gpio.mdio_num = ETH_MDIO_GPIO;
    // NOTE: ESP32-P4 RMII clock is sourced from the IP101 PHY (50 MHz input).
    // The ETH_ESP32_EMAC_DEFAULT_CONFIG should handle this for ESP32-P4.
    // If no link after reset, check sdkconfig for CONFIG_ETH_RMII_CLK_MODE.

    esp_eth_mac_t *mac = esp_eth_mac_new_esp32(&emac_config, &mac_config);
    if (!mac) {
        ESP_LOGE(TAG, "MAC create failed");
        return ESP_FAIL;
    }

    // ── PHY (IP101) ───────────────────────────────────────────────
    eth_phy_config_t phy_config = ETH_PHY_DEFAULT_CONFIG();
    phy_config.phy_addr        = ETH_PHY_ADDR;
    phy_config.reset_gpio_num  = ETH_PHY_RST_GPIO;

    esp_eth_phy_t *phy = esp_eth_phy_new_ip101(&phy_config);
    if (!phy) {
        ESP_LOGE(TAG, "PHY create failed");
        return ESP_FAIL;
    }

    // ── Ethernet driver ───────────────────────────────────────────
    static esp_eth_handle_t eth_handle = NULL;
    esp_eth_config_t eth_config = ETH_DEFAULT_CONFIG(mac, phy);

    ret = esp_eth_driver_install(&eth_config, &eth_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "ETH driver install failed: %s", esp_err_to_name(ret));
        return ret;
    }

    // Glue netif to eth driver
    esp_netif_attach(eth_netif, esp_eth_new_netif_glue(eth_handle));

    // Register event handlers
    esp_event_handler_register(ETH_EVENT, ESP_EVENT_ANY_ID,
                               eth_event_handler, &eth_handle);
    esp_event_handler_register(IP_EVENT, IP_EVENT_ETH_GOT_IP,
                               got_ip_event_handler, NULL);

    // Start Ethernet
    ret = esp_eth_start(eth_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "ETH start failed: %s", esp_err_to_name(ret));
        return ret;
    }

    // Wait up to 30 s for IP
    ESP_LOGI(TAG, "Waiting for IP address (up to 30 s)...");
    EventBits_t bits = xEventGroupWaitBits(s_eth_event_group,
                                           ETH_GOT_IP_BIT,
                                           pdFALSE, pdFALSE,
                                           pdMS_TO_TICKS(30000));
    if (bits & ETH_GOT_IP_BIT) {
        ESP_LOGI(TAG, "Ethernet connected: %s", s_ip_str);
    } else {
        ESP_LOGW(TAG, "Ethernet timeout — continuing without IP");
    }

    return ESP_OK;
}

bool eth_is_connected(void)
{
    return s_connected;
}

bool eth_get_ip_str(char *ip_str, size_t buf_size)
{
    if (!s_connected || strlen(s_ip_str) == 0) return false;
    strlcpy(ip_str, s_ip_str, buf_size);
    return true;
}

void eth_get_mac(uint8_t mac[6])
{
    memcpy(mac, s_mac, 6);
}
