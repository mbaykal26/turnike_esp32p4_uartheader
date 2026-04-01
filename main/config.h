#pragma once
/*
 * ╔══════════════════════════════════════════════════════════════╗
 * ║  ESP32-P4 Access Control – config.h                         ║
 * ║  Waveshare ESP32-P4-WIFI6-POE-ETH                           ║
 * ║  All pins, credentials and timing in one place              ║
 * ╚══════════════════════════════════════════════════════════════╝
 */

#include "driver/gpio.h"
#include "driver/uart.h"

// ─────────────────────────────────────────────────────────────────
// PN532 NFC Reader  (Hardware SPI – same GPIO as Arduino project)
// DIP switches on PN532 board: SW1=ON, SW2=OFF  (SPI mode)
// ─────────────────────────────────────────────────────────────────
#define NFC_SCK_PIN     GPIO_NUM_20
#define NFC_MISO_PIN    GPIO_NUM_22   // MISO ← PN532 SDO  (GPIO22 confirmed by wire=0xFF test)
#define NFC_MOSI_PIN    GPIO_NUM_21   // MOSI → PN532 SDI  (GPIO21 confirmed by wire=0x00 test)
#define NFC_CS_PIN      GPIO_NUM_23
#define NFC_RST_PIN     GPIO_NUM_NC   // Optional: connect to PN532 RSTPD_N for hardware reset
#define NFC_SPI_HOST    SPI2_HOST
#define NFC_SPI_FREQ_HZ (1000 * 1000)        // 1 MHz — standard operating speed

// ─────────────────────────────────────────────────────────────────
// GM861 Barcode Scanner  (UART1)
// Wired to the SH1.0 4-PIN UART header (#6 on board silkscreen).
// GPIO4  = UART1 RX (ESP32 RX ← GM861 TX)
// GPIO5  = UART1 TX (ESP32 TX → GM861 RX)
// NOTE: GPIO37/38 are the console UART (USB-UART bridge) — do NOT use
// those for UART1 or the serial monitor dies after UART1 init.
// ─────────────────────────────────────────────────────────────────
#define BARCODE_UART_NUM    UART_NUM_1
#define BARCODE_RX_PIN      GPIO_NUM_4
#define BARCODE_TX_PIN      GPIO_NUM_5
#define BARCODE_BAUD        115200
#define BARCODE_BUF_SIZE    512
#define BARCODE_MAX_LEN     128
#define BARCODE_TIMEOUT_MS  1000   // read window before giving up

// ─────────────────────────────────────────────────────────────────
// Status LEDs
// ─────────────────────────────────────────────────────────────────
#define LED_GREEN_PIN   GPIO_NUM_1
#define LED_RED_PIN     GPIO_NUM_2
#define LED_ON_TIME_MS  1000

// ─────────────────────────────────────────────────────────────────
// Audio – ES8311 codec + NS4150B amp  (CONFIRMED WORKING – v4)
// DOUT=GPIO9  → ES8311 DSDIN  (DAC input)   ← KEY FIX
// DIN =GPIO11 ← ES8311 ASDOUT (ADC output)
// ─────────────────────────────────────────────────────────────────
#define I2S_MCLK_PIN    GPIO_NUM_13
#define I2S_BCLK_PIN    GPIO_NUM_12
#define I2S_LRCK_PIN    GPIO_NUM_10
#define I2S_DOUT_PIN    GPIO_NUM_9    // → ES8311 DSDIN (DAC)  FIXED!
#define I2S_DIN_PIN     GPIO_NUM_11   // ← ES8311 ASDOUT (ADC)
#define PA_CTRL_PIN     GPIO_NUM_53   // NS4150B amp enable HIGH=ON
#define I2C_SDA_PIN     GPIO_NUM_7
#define I2C_SCL_PIN     GPIO_NUM_8
#define ES8311_I2C_ADDR 0x18
#define AUDIO_SAMPLE_RATE 16000

// Tone definitions
#define TONE_GRANT_1_HZ     1000    // first grant tone  (Hz)
#define TONE_GRANT_2_HZ     1500    // second grant tone (Hz)
#define TONE_GRANT_DUR_MS   200     // each tone duration
#define TONE_GRANT_GAP_MS   100     // silence gap between tones
#define TONE_DENY_HZ        700     // deny tone (Hz) before it was 800 Hz.
#define TONE_DENY_DUR_MS    400
#define TONE_AMPLITUDE      28000   // 16-bit peak (max 32767)
#define TONE_FADE_SAMPLES   80      // ~5 ms fade to prevent pops

// ─────────────────────────────────────────────────────────────────
// Ethernet – IP101 PHY, Internal EMAC (RMII)
// ─────────────────────────────────────────────────────────────────
#define ETH_MDC_GPIO        GPIO_NUM_31
#define ETH_MDIO_GPIO       GPIO_NUM_52
#define ETH_PHY_RST_GPIO    GPIO_NUM_51
#define ETH_PHY_ADDR        1
#define DEVICE_HOSTNAME     "esp32-p4-access"

// ─────────────────────────────────────────────────────────────────
// Telnet Server
// ─────────────────────────────────────────────────────────────────
#define TELNET_PORT         23
#define TELNET_MAX_CLIENTS  3

// ─────────────────────────────────────────────────────────────────
// API backend selection
// Set to 1 to use PythonAnywhere card-access API.
// Set to 0 to use Anadolu University API (access_check.c).
// Only one should be active at a time.
// ─────────────────────────────────────────────────────────────────
#define USE_PA_API  0

// ─────────────────────────────────────────────────────────────────
// PythonAnywhere card-access API  (pa_access_check.c)
// ─────────────────────────────────────────────────────────────────
#define PA_ACCESS_URL  "https://mbaykal.pythonanywhere.com/api/card-access"

// ─────────────────────────────────────────────────────────────────
// Status Reporter  (PythonAnywhere dashboard)
// ─────────────────────────────────────────────────────────────────
#define DEVICE_NAME     "Eczacılık Fakültesi 1"
#define REPORTER_URL    "https://mbaykal.pythonanywhere.com/api/turnstile_status"

// ─────────────────────────────────────────────────────────────────
// Web Service  (Anadolu University Access Control API)
// ─────────────────────────────────────────────────────────────────
// OLD endpoint (dis-erisim): response keys Sonuc/Ad/Soyad, terminalId as string
// #define API_URL  "https://anages.anadolu.edu.tr/api/dis-erisim/kart-erisim-arge"

// NEW endpoint (online-erisim): response keys sonuc/isim/mesaj, terminalId as JSON number
#define API_URL \
    "https://anages.anadolu.edu.tr/api/terminal/online-erisim"

/* Bearer token is in secrets.h — keep that file private */
#include "secrets.h"

// NOTE: API_TERMINAL_ID is rendered without quotes in JSON body ("terminalId":203 not "203").
// #define API_TERMINAL_ID     "203"
#define API_TERMINAL_ID     "75379662"
#define API_TIMEOUT_MS      5000

// ─────────────────────────────────────────────────────────────────
// Debug / Performance Settings
// ─────────────────────────────────────────────────────────────────
// Set to 0 for production (faster, less serial output)
// Set to 1 for debugging (verbose logging)
#define DEBUG_VERBOSE_LOGGING   0

// ─────────────────────────────────────────────────────────────────
// Timing / Loop Constants
// ─────────────────────────────────────────────────────────────────
#define CARD_READ_DELAY_MS          2000   // duplicate detection window
#define NFC_HEALTH_CHECK_MS         30000  // health check interval (reduced frequency)
#define NFC_WATCHDOG_TIMEOUT_MS     60000  // max silent time (increased from 30s)
#define NFC_RETRY_DELAY_MS          5000   // wait before retry after fail
#define STATUS_HEARTBEAT_MS         30000  // serial/telnet heartbeat
