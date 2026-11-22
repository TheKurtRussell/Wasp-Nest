/*
 * Wasp Nest — C6 Wi-Fi recon sniffer (implementation scaffold)
 *
 * Mirrors hc_radio_sniffer.c: a FreeRTOS queue decouples the promiscuous RX
 * callback from a worker task, so the callback stays short and never blocks the
 * Wi-Fi driver. The worker parses management frames into hc_wifi_ap_evt_t and
 * invokes the user callbacks (which hc_mode_agent wires to the link).
 *
 * Recon only: WIFI_MODE_NULL + promiscuous. No STA/AP association, no transmit.
 */
#include "hc_radio_wifi_sniffer.h"

#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_wifi.h"
#include "esp_timer.h"
#include "esp_log.h"
#include "esp_mac.h"

static const char *TAG = "hc_wifi_sniff";

/* --- 802.11 bits we need (keep local; a fuller set lives in hc_wifi_defs.h) - */
#define WIFI_FC_TYPE_MGMT        0x0
#define WIFI_FC_SUBTYPE_ASSOC_REQ 0x0
#define WIFI_FC_SUBTYPE_PROBE_REQ 0x4
#define WIFI_FC_SUBTYPE_PROBE_RSP 0x5
#define WIFI_FC_SUBTYPE_BEACON    0x8

#define EID_SSID                 0x00   /* tagged param: SSID */
#define EID_DSSS                 0x03   /* DS Parameter Set: current channel */
#define EID_RSN                  0x30   /* RSN (for WPA2/WPA3 heuristic) */

/* A raw capture handed from the RX callback to the worker task. We copy only the
 * header + tagged params we care about to keep the queue element bounded. */
#define RX_SNAP_MAX              256

typedef struct {
    uint64_t ts_us;
    uint8_t  channel;
    int8_t   rssi;
    uint16_t len;              /* bytes copied into buf (<= RX_SNAP_MAX) */
    uint16_t orig_len;         /* full frame length on air */
    uint8_t  buf[RX_SNAP_MAX];
} rx_snap_t;

static QueueHandle_t             s_q;
static TaskHandle_t              s_task;
static volatile bool             s_running;
static hc_wifi_sniffer_callbacks_t s_cb;
static hc_wscan_cfg_t            s_cfg;
static volatile bool             s_want_raw;

/* ---- 802.11 management-frame header (little byte fiddling, no full struct) - */
/* frame control (2) | duration (2) | addr1 (6) | addr2 (6) | addr3 (6) |
 * seq (2) | [beacon/probe-resp fixed params 12] | tagged params ...          */
#define MGMT_HDR_LEN   24
#define BEACON_FIXED   12   /* timestamp(8) + beacon int(2) + cap(2) */

static uint8_t fc_type(const uint8_t *f)    { return (f[0] >> 2) & 0x3; }
static uint8_t fc_subtype(const uint8_t *f) { return (f[0] >> 4) & 0xF; }

/* Walk tagged parameters, filling ssid + channel + flags. `tags`/`tags_len`
 * point at the first EID after the fixed params. */
static void parse_tags(const uint8_t *tags, int tags_len, hc_wifi_ap_evt_t *ap)
{
    int i = 0;
    while (i + 2 <= tags_len) {
        uint8_t eid = tags[i];
        uint8_t len = tags[i + 1];
        if (i + 2 + len > tags_len) break;
        const uint8_t *val = &tags[i + 2];
        switch (eid) {
        case EID_SSID:
            if (len == 0) {
                ap->flags |= HC_WIFI_F_HIDDEN;
            } else {
                uint8_t n = len > 32 ? 32 : len;
                memcpy(ap->ssid, val, n);
                ap->ssid_len = n;
            }
            break;
        case EID_DSSS:
            if (len >= 1) ap->channel = val[0];   /* authoritative channel */
            break;
        case EID_RSN:
            /* Best-effort WPA3: look for SAE AKM suite 00-0F-AC:8 in the RSN
             * element. Left as a focused TODO for on-hardware verification. */
            /* if (rsn_has_sae(val, len)) ap->flags |= HC_WIFI_F_WPA3; */
            break;
        default: break;
        }
        i += 2 + len;
    }
}

static void worker_task(void *arg)
{
    rx_snap_t snap;
    while (s_running) {
        if (xQueueReceive(s_q, &snap, pdMS_TO_TICKS(200)) != pdTRUE) continue;
        if (snap.len < MGMT_HDR_LEN) continue;

        const uint8_t *f = snap.buf;
        if (fc_type(f) != WIFI_FC_TYPE_MGMT) continue;
        uint8_t st = fc_subtype(f);

        hc_wifi_ap_evt_t ap = {0};
        ap.rssi    = snap.rssi;
        ap.channel = snap.channel;        /* fallback; DSSS tag overrides */
        memcpy(ap.bssid, &f[10], 6);      /* addr2 = transmitter/BSSID */

        int tags_off, tags_len;
        if (st == WIFI_FC_SUBTYPE_BEACON || st == WIFI_FC_SUBTYPE_PROBE_RSP) {
            tags_off = MGMT_HDR_LEN + BEACON_FIXED;
        } else if (st == WIFI_FC_SUBTYPE_PROBE_REQ) {
            ap.flags |= HC_WIFI_F_PROBE;
            tags_off = MGMT_HDR_LEN;      /* no fixed params in a probe req */
        } else {
            continue;                     /* ignore other mgmt subtypes here */
        }
        tags_len = (int)snap.len - tags_off;
        if (tags_len > 0) parse_tags(&f[tags_off], tags_len, &ap);

        if (s_cb.on_ap) s_cb.on_ap(&ap, s_cb.ctx);

        if (s_want_raw && s_cb.on_frame) {
            hc_frame_hdr_t meta = {
                .ts_us = snap.ts_us, .channel = snap.channel,
                .rssi = snap.rssi, .lqi = 0, .fcs_ok = 1,
                .len = snap.orig_len,
            };
            s_cb.on_frame(&meta, snap.buf, s_cb.ctx);
        }
    }
    s_task = NULL;
    vTaskDelete(NULL);
}

/* esp_wifi promiscuous callback — runs in Wi-Fi task context. Keep it tiny:
 * snapshot and enqueue, never parse here. */
static void promisc_cb(void *buf, wifi_promiscuous_pkt_type_t type)
{
    if (type != WIFI_PKT_MGMT || !s_running) return;
    const wifi_promiscuous_pkt_t *p = (const wifi_promiscuous_pkt_t *)buf;

    rx_snap_t snap;
    snap.ts_us    = esp_timer_get_time();
    snap.channel  = p->rx_ctrl.channel;
    snap.rssi     = p->rx_ctrl.rssi;
    snap.orig_len = p->rx_ctrl.sig_len;
    snap.len      = p->rx_ctrl.sig_len > RX_SNAP_MAX ? RX_SNAP_MAX
                                                     : p->rx_ctrl.sig_len;
    memcpy(snap.buf, p->payload, snap.len);

    /* Drop on a full queue rather than block the Wi-Fi driver. */
    xQueueSendFromISR(s_q, &snap, NULL);
}

esp_err_t hc_wifi_sniffer_init(const hc_wifi_sniffer_callbacks_t *cb)
{
    if (!cb) return ESP_ERR_INVALID_ARG;
    s_cb = *cb;

    wifi_init_config_t ic = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&ic));
    ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_NULL));
    ESP_ERROR_CHECK(esp_wifi_start());

    s_q = xQueueCreate(32, sizeof(rx_snap_t));
    if (!s_q) return ESP_ERR_NO_MEM;

    ESP_LOGI(TAG, "wifi recon sniffer ready");
    return ESP_OK;
}

esp_err_t hc_wifi_sniffer_deinit(void)
{
    hc_wifi_sniffer_stop();
    esp_wifi_stop();
    esp_wifi_deinit();
    if (s_q) { vQueueDelete(s_q); s_q = NULL; }
    return ESP_OK;
}

esp_err_t hc_wifi_sniffer_start(const hc_wscan_cfg_t *cfg)
{
    if (!cfg || s_running) return ESP_ERR_INVALID_STATE;
    if (!(cfg->band_mask & HC_WBAND_2G4)) {
        ESP_LOGW(TAG, "C6 serves 2.4 GHz only; 5 GHz is a C5-forager job");
        return ESP_ERR_NOT_SUPPORTED;
    }
    s_cfg      = *cfg;
    s_want_raw = cfg->want_raw;

    wifi_promiscuous_filter_t filt = { .filter_mask = WIFI_PROMIS_FILTER_MASK_MGMT };
    ESP_ERROR_CHECK(esp_wifi_set_promiscuous_filter(&filt));
    ESP_ERROR_CHECK(esp_wifi_set_promiscuous_rx_cb(&promisc_cb));
    ESP_ERROR_CHECK(esp_wifi_set_promiscuous(true));

    uint8_t ch = cfg->hop_count ? cfg->hop[0] : 1;
    esp_wifi_set_channel(ch, WIFI_SECOND_CHAN_NONE);

    s_running = true;
    xTaskCreate(worker_task, "wifi_sniff", 4096, NULL, 5, &s_task);

    /* Channel hopping: a lightweight timer or a tick in the worker can rotate
     * through cfg.hop[] every cfg.hop_dwell_ms. Kept out of the first slice so
     * a fixed-channel scan is testable before hop timing is tuned. TODO. */
    ESP_LOGI(TAG, "recon started: ch=%u hop=%u dwell=%ums raw=%u",
             ch, cfg->hop_count, cfg->hop_dwell_ms, cfg->want_raw);
    return ESP_OK;
}

esp_err_t hc_wifi_sniffer_stop(void)
{
    if (!s_running) return ESP_OK;
    s_running = false;
    esp_wifi_set_promiscuous(false);
    /* worker_task exits on the next queue timeout and self-deletes */
    return ESP_OK;
}

esp_err_t hc_wifi_sniffer_set_channel(uint8_t channel)
{
    if (channel < 1 || channel > 14) return ESP_ERR_INVALID_ARG;
    return esp_wifi_set_channel(channel, WIFI_SECOND_CHAN_NONE);
}

bool hc_wifi_sniffer_is_running(void) { return s_running; }
