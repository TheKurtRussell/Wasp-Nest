/*
 * Wasp Nest — P4 mode manager implementation
 */
#include "hc_mode_manager.h"
#include "hc_link_host.h"
#include "hc_explorer.h"
#include "hc_wifi_recon.h"
#include "hc_ble_recon.h"
#include "hc_board.h"

#include <string.h>
#include "esp_log.h"
#include "esp_check.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define TAG "hc_mm"

static hc_radio_mode_t s_mode = HC_MODE_IDLE;
static hc_mm_mode_cb_t s_mode_cb;
static void           *s_ctx;

/* Link RX fan-out: translate wire events into explorer/mode callbacks. */
static void on_link_rx(hc_opcode_t op, const uint8_t *p, uint16_t len, void *ctx)
{
    switch (op) {
    case HC_EV_FRAME:
        if (len >= sizeof(hc_frame_hdr_t)) {
            const hc_frame_hdr_t *m = (const hc_frame_hdr_t *)p;
            hc_explorer_on_frame(m, p + sizeof(*m));
        }
        break;
    case HC_EV_ENERGY:
        if (len >= sizeof(hc_energy_result_t))
            hc_explorer_on_energy((const hc_energy_result_t *)p);
        break;
    case HC_EV_SCAN_RESULT:
        if (len >= sizeof(hc_scan_result_t))
            hc_explorer_on_scan((const hc_scan_result_t *)p);
        break;
    case HC_EV_MODE:
        if (len >= sizeof(hc_mode_state_t)) {
            const hc_mode_state_t *st = (const hc_mode_state_t *)p;
            s_mode = (hc_radio_mode_t)st->mode;
            if (s_mode_cb) s_mode_cb(s_mode, st->ok, st->reason, s_ctx);
        }
        break;
    case HC_EV_WIFI_AP:
        if (len >= sizeof(hc_wifi_ap_evt_t))
            hc_wifi_recon_on_ap((const hc_wifi_ap_evt_t *)p);
        break;
    case HC_EV_BLE_DEV:
        if (len >= sizeof(hc_ble_dev_evt_t))
            hc_ble_recon_on_dev((const hc_ble_dev_evt_t *)p);
        break;
    case HC_EV_LOG:
        if (len >= 1) ESP_LOGI("c6", "%s", ((const hc_log_t *)p)->msg);
        break;
    default: break;
    }
}

void hc_mode_manager_reset_c6(void)
{
    hc_board_c6_reset();   /* pulse the RESET GPIO on the SDIO2 bus */
}

esp_err_t hc_mode_manager_start(hc_mm_mode_cb_t on_mode, void *ctx)
{
    s_mode_cb = on_mode; s_ctx = ctx;

    ESP_RETURN_ON_ERROR(hc_link_host_init(on_link_rx, NULL), TAG, "link");

    /* Make sure the C6 is alive and in a known (post-reset) state. */
    hc_board_c6_reset();
    vTaskDelay(pdMS_TO_TICKS(300));

    hc_hello_t c6;
    esp_err_t e = hc_link_host_handshake(&c6, 2000);
    if (e != ESP_OK) {
        ESP_LOGE(TAG, "C6 handshake failed: %s", esp_err_to_name(e));
        return e;
    }
    ESP_LOGI(TAG, "C6 online: %s", c6.fw_semver);
    return ESP_OK;
}

esp_err_t hc_mode_manager_set_mode(hc_radio_mode_t mode)
{
    hc_set_mode_t m = { .mode = (uint8_t)mode };
    return hc_link_host_send(HC_OP_SET_MODE, &m, sizeof(m));
    /* The C6 replies HC_EV_MODE; if it was a WIFI switch the C6 reboots and the
     * esp_hosted slave takes the bus. The P4's Wi-Fi stack (esp_wifi_remote)
     * must then (re)init — handled by the WIFI screen, phase 2. */
}
hc_radio_mode_t hc_mode_manager_mode(void) { return s_mode; }

esp_err_t hc_mode_manager_sniff_start(const hc_sniff_cfg_t *cfg)
{ return hc_link_host_send(HC_OP_SNIFF_START, cfg, sizeof(*cfg)); }

esp_err_t hc_mode_manager_sniff_stop(void)
{ return hc_link_host_send(HC_OP_SNIFF_STOP, NULL, 0); }

esp_err_t hc_mode_manager_set_channel(uint8_t channel)
{ hc_set_chan_t c = { .channel = channel };
  return hc_link_host_send(HC_OP_SNIFF_SET_CHAN, &c, sizeof(c)); }

esp_err_t hc_mode_manager_energy_scan(uint32_t mask, uint16_t dwell)
{ hc_energy_scan_t s = { .channel_mask = mask, .dwell_ms = dwell };
  return hc_link_host_send(HC_OP_SCAN_ENERGY, &s, sizeof(s)); }

esp_err_t hc_mode_manager_active_scan(uint32_t mask, uint16_t dwell)
{ hc_active_scan_t s = { .channel_mask = mask, .dwell_ms = dwell };
  return hc_link_host_send(HC_OP_SCAN_ACTIVE, &s, sizeof(s)); }

/* ---- Wi-Fi recon control (valid only in HC_MODE_WIFI_SNIFFER) ------------- */
esp_err_t hc_mode_manager_wifi_scan_start(const hc_wscan_cfg_t *cfg)
{ return hc_link_host_send(HC_OP_WSCAN_START, cfg, sizeof(*cfg)); }

esp_err_t hc_mode_manager_wifi_scan_stop(void)
{ return hc_link_host_send(HC_OP_WSCAN_STOP, NULL, 0); }

esp_err_t hc_mode_manager_wifi_set_channel(uint8_t channel)
{ hc_set_chan_t c = { .channel = channel };
  return hc_link_host_send(HC_OP_WSCAN_SET_CHAN, &c, sizeof(c)); }

/* ---- BLE recon control (valid only in HC_MODE_BLE_SCANNER) ---------------- */
esp_err_t hc_mode_manager_ble_scan_start(const hc_blescan_cfg_t *cfg)
{ return hc_link_host_send(HC_OP_BLESCAN_START, cfg, sizeof(*cfg)); }

esp_err_t hc_mode_manager_ble_scan_stop(void)
{ return hc_link_host_send(HC_OP_BLESCAN_STOP, NULL, 0); }
