/*
 * gm805_uart.c – GM805 barcode scanner driver for ESP-IDF
 *
 * The GM805 sends barcodes as ASCII text terminated by CR (0x0D) or
 * LF (0x0A).  We use UART1 at 115200 baud, 8N1.
 */

#include "gm805_uart.h"
#include "config.h"

#include <string.h>
#include "driver/uart.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_timer.h"

static const char *TAG = "gm805";

esp_err_t gm805_init(void)
{
    ESP_LOGI(TAG, "GM805 UART%d: RX=GPIO%d TX=GPIO%d baud=%d",
             BARCODE_UART_NUM, BARCODE_RX_PIN, BARCODE_TX_PIN, BARCODE_BAUD);

    uart_config_t uart_cfg = {
        .baud_rate  = BARCODE_BAUD,
        .data_bits  = UART_DATA_8_BITS,
        .parity     = UART_PARITY_DISABLE,
        .stop_bits  = UART_STOP_BITS_1,
        .flow_ctrl  = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };

    esp_err_t ret;

    ret = uart_driver_install(BARCODE_UART_NUM, BARCODE_BUF_SIZE, 0, 0, NULL, 0);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "UART driver install failed: %s", esp_err_to_name(ret));
        return ret;
    }

    ret = uart_param_config(BARCODE_UART_NUM, &uart_cfg);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "UART param config failed: %s", esp_err_to_name(ret));
        return ret;
    }

    ret = uart_set_pin(BARCODE_UART_NUM,
                       BARCODE_TX_PIN, BARCODE_RX_PIN,
                       UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "UART set pin failed: %s", esp_err_to_name(ret));
        return ret;
    }

    // Flush UART buffer to clear any garbage from power-on
    uart_flush(BARCODE_UART_NUM);
    vTaskDelay(pdMS_TO_TICKS(50));  // Let scanner stabilize
    uart_flush(BARCODE_UART_NUM);

    ESP_LOGI(TAG, "GM805 UART ready");
    return ESP_OK;
}

bool gm805_read_barcode(char *buf, size_t buf_size, uint32_t timeout_ms)
{
    if (!buf || buf_size < 2) return false;

    size_t  pos      = 0;
    bool    got_data = false;
    int64_t deadline = esp_timer_get_time() + (int64_t)timeout_ms * 1000;

    while (esp_timer_get_time() < deadline) {
        uint8_t ch;
        int n = uart_read_bytes(BARCODE_UART_NUM, &ch, 1,
                                pdMS_TO_TICKS(10));
        if (n <= 0) continue;

        // CR or LF = end of barcode
        if (ch == '\r' || ch == '\n') {
            if (pos > 0) {
                // drain any trailing CR/LF
                vTaskDelay(pdMS_TO_TICKS(5));
                uint8_t trash[8];
                uart_read_bytes(BARCODE_UART_NUM, trash, sizeof(trash),
                                pdMS_TO_TICKS(10));
                break;
            }
            continue;   // ignore leading CR/LF
        }

        // Filter to printable ASCII only
        if (ch < 0x20 || ch > 0x7E) continue;

        if (pos < buf_size - 1) {
            buf[pos++] = (char)ch;
            got_data = true;
        }
    }

    buf[pos] = '\0';
    return got_data && pos > 0;
}
