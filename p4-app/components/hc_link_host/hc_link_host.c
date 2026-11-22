/*
 * Wasp Nest — P4 link layer (UART backend)
 * Byte-for-byte compatible with hc_link_slave.c on the C6.
 */
#include "hc_link_host.h"

#include <string.h>
#include "driver/uart.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "freertos/event_groups.h"
#include "esp_log.h"
#include "esp_check.h"

#define TAG "hc_link"

/* P4 end of the link UART. Pins are the M5-Bus debug/aux UART; see
 * docs/hardware.md. Keep TX/RX crossed vs the C6 side. */
#define HC_LINK_UART_NUM   UART_NUM_1
#define HC_LINK_UART_TX    6     /* P4 GPIO -> C6 RX */
#define HC_LINK_UART_RX    7     /* P4 GPIO <- C6 TX */
#define HC_LINK_BAUD       921600
#define HC_LINK_BUF        4096

static hc_link_rx_cb_t s_cb;
static void           *s_ctx;
static SemaphoreHandle_t s_tx_mutex;
static uint16_t        s_tx_seq;

/* handshake plumbing */
static EventGroupHandle_t s_ev;
#define EV_HELLO  BIT0
static hc_hello_t s_c6_hello;

static uint16_t crc16(uint16_t crc, const uint8_t *d, uint16_t n)
{
    for (uint16_t i = 0; i < n; ++i) {
        crc ^= (uint16_t)d[i] << 8;
        for (int b = 0; b < 8; ++b)
            crc = (crc & 0x8000) ? (crc << 1) ^ 0x1021 : (crc << 1);
    }
    return crc;
}

esp_err_t hc_link_host_send(hc_opcode_t op, const void *payload, uint16_t len)
{
    if (len > HC_MAX_PAYLOAD) return ESP_ERR_INVALID_ARG;
    xSemaphoreTake(s_tx_mutex, portMAX_DELAY);
    hc_link_hdr_t hdr = { .magic = HC_LINK_MAGIC, .opcode = (uint8_t)op,
                          .flags = 0, .seq = s_tx_seq++, .len = len };
    uint16_t crc = 0xFFFF;
    crc = crc16(crc, (const uint8_t *)&hdr + 2, sizeof(hdr) - 2);
    if (len) crc = crc16(crc, payload, len);
    uart_write_bytes(HC_LINK_UART_NUM, &hdr, sizeof(hdr));
    if (len) uart_write_bytes(HC_LINK_UART_NUM, payload, len);
    uart_write_bytes(HC_LINK_UART_NUM, &crc, sizeof(crc));
    xSemaphoreGive(s_tx_mutex);
    return ESP_OK;
}

static void dispatch(hc_opcode_t op, const uint8_t *p, uint16_t len)
{
    /* intercept HELLO for the handshake, then pass everything to the app cb */
    if (op == HC_EV_HELLO && len >= sizeof(hc_hello_t)) {
        memcpy(&s_c6_hello, p, sizeof(s_c6_hello));
        if (s_ev) xEventGroupSetBits(s_ev, EV_HELLO);
    }
    if (s_cb) s_cb(op, p, len, s_ctx);
}

static void rx_task(void *arg)
{
    uint8_t buf[sizeof(hc_link_hdr_t) + HC_MAX_PAYLOAD + 2];
    for (;;) {
        uint8_t b0, b1;
        if (uart_read_bytes(HC_LINK_UART_NUM, &b0, 1, portMAX_DELAY) != 1) continue;
        if (b0 != (HC_LINK_MAGIC & 0xFF)) continue;
        if (uart_read_bytes(HC_LINK_UART_NUM, &b1, 1, pdMS_TO_TICKS(50)) != 1) continue;
        if (b1 != (HC_LINK_MAGIC >> 8)) continue;

        hc_link_hdr_t *hdr = (hc_link_hdr_t *)buf;
        hdr->magic = HC_LINK_MAGIC;
        if (uart_read_bytes(HC_LINK_UART_NUM, (uint8_t *)hdr + 2, sizeof(*hdr) - 2,
                            pdMS_TO_TICKS(50)) != (int)(sizeof(*hdr) - 2)) continue;
        if (hdr->len > HC_MAX_PAYLOAD) continue;

        uint8_t *payload = buf + sizeof(*hdr);
        if (hdr->len &&
            uart_read_bytes(HC_LINK_UART_NUM, payload, hdr->len, pdMS_TO_TICKS(100)) != hdr->len)
            continue;
        uint16_t rx_crc;
        if (uart_read_bytes(HC_LINK_UART_NUM, &rx_crc, 2, pdMS_TO_TICKS(50)) != 2) continue;

        uint16_t crc = 0xFFFF;
        crc = crc16(crc, (uint8_t *)hdr + 2, sizeof(*hdr) - 2);
        if (hdr->len) crc = crc16(crc, payload, hdr->len);
        if (crc != rx_crc) { ESP_LOGW(TAG, "crc drop op=0x%02x", hdr->opcode); continue; }

        dispatch((hc_opcode_t)hdr->opcode, payload, hdr->len);
    }
}

esp_err_t hc_link_host_init(hc_link_rx_cb_t cb, void *ctx)
{
    s_cb = cb; s_ctx = ctx;
    s_tx_mutex = xSemaphoreCreateMutex();
    s_ev = xEventGroupCreate();

    uart_config_t uc = {
        .baud_rate = HC_LINK_BAUD, .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE, .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE, .source_clk = UART_SCLK_DEFAULT,
    };
    ESP_RETURN_ON_ERROR(uart_driver_install(HC_LINK_UART_NUM, HC_LINK_BUF, HC_LINK_BUF, 0, NULL, 0), TAG, "drv");
    ESP_RETURN_ON_ERROR(uart_param_config(HC_LINK_UART_NUM, &uc), TAG, "cfg");
    ESP_RETURN_ON_ERROR(uart_set_pin(HC_LINK_UART_NUM, HC_LINK_UART_TX, HC_LINK_UART_RX,
                                     UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE), TAG, "pin");
    xTaskCreate(rx_task, "hc_link_rx", 4096, NULL, 12, NULL);
    ESP_LOGI(TAG, "link up (uart%d @ %d)", HC_LINK_UART_NUM, HC_LINK_BAUD);
    return ESP_OK;
}

esp_err_t hc_link_host_handshake(hc_hello_t *c6_out, uint32_t timeout_ms)
{
    xEventGroupClearBits(s_ev, EV_HELLO);
    hc_hello_t h = { .proto_version = HC_PROTO_VERSION, .chip = 0, .fw_build = 0 };
    strncpy(h.fw_semver, "0.1.0", sizeof(h.fw_semver));
    hc_link_host_send(HC_OP_HELLO, &h, sizeof(h));

    EventBits_t bits = xEventGroupWaitBits(s_ev, EV_HELLO, pdTRUE, pdTRUE,
                                           pdMS_TO_TICKS(timeout_ms));
    if (!(bits & EV_HELLO)) return ESP_ERR_TIMEOUT;
    if (s_c6_hello.proto_version != HC_PROTO_VERSION) {
        ESP_LOGE(TAG, "proto mismatch: P4=%d C6=%d", HC_PROTO_VERSION, s_c6_hello.proto_version);
        return ESP_ERR_INVALID_VERSION;
    }
    if (c6_out) *c6_out = s_c6_hello;
    ESP_LOGI(TAG, "C6 up: fw %s proto %d", s_c6_hello.fw_semver, s_c6_hello.proto_version);
    return ESP_OK;
}
