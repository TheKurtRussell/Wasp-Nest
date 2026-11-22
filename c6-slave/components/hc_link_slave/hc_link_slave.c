/*
 * Wasp Nest — C6 link layer (UART backend for bring-up)
 *
 * Wire format (both directions):
 *     [hc_link_hdr_t][payload ...][crc16]
 * CRC-16/CCITT-FALSE over hdr(after magic)+payload. Magic resyncs the stream
 * after any corruption. This is the same code path the P4's hc_link_host uses;
 * keep the two byte-for-byte compatible.
 */
#include "hc_link_slave.h"

#include <string.h>
#include <stdarg.h>
#include <stdio.h>
#include "driver/uart.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_log.h"
#include "esp_check.h"

#define TAG "hc_link"

/* ---- UART backend config (M5-Bus spare pins; see docs/hardware.md) -------- */
#define HC_LINK_UART_NUM   UART_NUM_1
#define HC_LINK_UART_TX    4        /* C6 GPIO -> P4 RX  (adjust per wiring) */
#define HC_LINK_UART_RX    5        /* C6 GPIO <- P4 TX */
#define HC_LINK_BAUD       921600
#define HC_LINK_BUF        2048

static hc_link_rx_cb_t s_cb;
static void           *s_ctx;
static SemaphoreHandle_t s_tx_mutex;
static uint16_t        s_tx_seq;

/* CRC-16/CCITT-FALSE */
static uint16_t crc16(uint16_t crc, const uint8_t *d, uint16_t n)
{
    for (uint16_t i = 0; i < n; ++i) {
        crc ^= (uint16_t)d[i] << 8;
        for (int b = 0; b < 8; ++b)
            crc = (crc & 0x8000) ? (crc << 1) ^ 0x1021 : (crc << 1);
    }
    return crc;
}

esp_err_t hc_link_slave_send(hc_opcode_t op, const void *payload, uint16_t len)
{
    if (len > HC_MAX_PAYLOAD) return ESP_ERR_INVALID_ARG;
    xSemaphoreTake(s_tx_mutex, portMAX_DELAY);

    hc_link_hdr_t hdr = {
        .magic  = HC_LINK_MAGIC,
        .opcode = (uint8_t)op,
        .flags  = 0,
        .seq    = s_tx_seq++,
        .len    = len,
    };
    uint16_t crc = 0xFFFF;
    crc = crc16(crc, (const uint8_t *)&hdr + 2, sizeof(hdr) - 2); /* skip magic */
    if (len) crc = crc16(crc, payload, len);

    uart_write_bytes(HC_LINK_UART_NUM, &hdr, sizeof(hdr));
    if (len) uart_write_bytes(HC_LINK_UART_NUM, payload, len);
    uart_write_bytes(HC_LINK_UART_NUM, &crc, sizeof(crc));

    xSemaphoreGive(s_tx_mutex);
    return ESP_OK;
}

void hc_link_slave_logf(uint8_t level, const char *fmt, ...)
{
    hc_log_t log = { .level = level };
    va_list ap; va_start(ap, fmt);
    vsnprintf(log.msg, sizeof(log.msg), fmt, ap);
    va_end(ap);
    hc_link_slave_send(HC_EV_LOG, &log, sizeof(log));
}

/* Byte-oriented receiver: resync on magic, read hdr, payload, crc. */
static void rx_task(void *arg)
{
    uint8_t buf[sizeof(hc_link_hdr_t) + HC_MAX_PAYLOAD + 2];
    for (;;) {
        /* hunt for magic low/high */
        uint8_t b0, b1;
        if (uart_read_bytes(HC_LINK_UART_NUM, &b0, 1, portMAX_DELAY) != 1) continue;
        if (b0 != (HC_LINK_MAGIC & 0xFF)) continue;
        if (uart_read_bytes(HC_LINK_UART_NUM, &b1, 1, pdMS_TO_TICKS(50)) != 1) continue;
        if (b1 != (HC_LINK_MAGIC >> 8)) continue;

        hc_link_hdr_t *hdr = (hc_link_hdr_t *)buf;
        hdr->magic = HC_LINK_MAGIC;
        /* read remainder of header */
        int got = uart_read_bytes(HC_LINK_UART_NUM,
                                  (uint8_t *)hdr + 2, sizeof(*hdr) - 2,
                                  pdMS_TO_TICKS(50));
        if (got != (int)(sizeof(*hdr) - 2)) continue;
        if (hdr->len > HC_MAX_PAYLOAD) continue;

        uint8_t *payload = buf + sizeof(*hdr);
        if (hdr->len) {
            got = uart_read_bytes(HC_LINK_UART_NUM, payload, hdr->len, pdMS_TO_TICKS(100));
            if (got != hdr->len) continue;
        }
        uint16_t rx_crc;
        if (uart_read_bytes(HC_LINK_UART_NUM, &rx_crc, 2, pdMS_TO_TICKS(50)) != 2) continue;

        uint16_t crc = 0xFFFF;
        crc = crc16(crc, (uint8_t *)hdr + 2, sizeof(*hdr) - 2);
        if (hdr->len) crc = crc16(crc, payload, hdr->len);
        if (crc != rx_crc) { ESP_LOGW(TAG, "crc drop op=0x%02x", hdr->opcode); continue; }

        if (s_cb) s_cb((hc_opcode_t)hdr->opcode, payload, hdr->len, s_ctx);
    }
}

esp_err_t hc_link_slave_init(hc_link_rx_cb_t cb, void *ctx)
{
    s_cb = cb; s_ctx = ctx;
    s_tx_mutex = xSemaphoreCreateMutex();

    uart_config_t uc = {
        .baud_rate = HC_LINK_BAUD,
        .data_bits = UART_DATA_8_BITS,
        .parity    = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };
    ESP_RETURN_ON_ERROR(uart_driver_install(HC_LINK_UART_NUM, HC_LINK_BUF, HC_LINK_BUF, 0, NULL, 0), TAG, "drv");
    ESP_RETURN_ON_ERROR(uart_param_config(HC_LINK_UART_NUM, &uc), TAG, "cfg");
    ESP_RETURN_ON_ERROR(uart_set_pin(HC_LINK_UART_NUM, HC_LINK_UART_TX, HC_LINK_UART_RX,
                                     UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE), TAG, "pin");

    xTaskCreate(rx_task, "hc_link_rx", 4096, NULL, 11, NULL);
    ESP_LOGI(TAG, "link up (uart%d @ %d)", HC_LINK_UART_NUM, HC_LINK_BAUD);
    return ESP_OK;
}
