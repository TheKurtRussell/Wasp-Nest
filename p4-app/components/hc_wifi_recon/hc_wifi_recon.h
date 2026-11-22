/*
 * Wasp Nest — P4 Wi-Fi recon table
 *
 * The Wi-Fi counterpart to hc_explorer's role as a data model: it receives
 * HC_EV_WIFI_AP sightings from the C6 (via the mode manager) and dedupes them
 * by BSSID into a compact, thread-safe table the UI reads to draw a live AP list.
 *
 * Writes come from the link task; reads come from the LVGL task, so the table is
 * mutex-guarded. Recon only — no association, no attack.
 */
#ifndef HC_WIFI_RECON_H
#define HC_WIFI_RECON_H

#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"
#include "hc_link_protocol.h"

#ifdef __cplusplus
extern "C" {
#endif

/* One deduped access point, accumulated across sightings. */
typedef struct {
    uint8_t  bssid[6];
    char     ssid[33];   /* NUL-terminated; empty when hidden */
    uint8_t  ssid_len;
    uint8_t  channel;    /* last seen on */
    int8_t   rssi;       /* strongest seen */
    uint8_t  flags;      /* accumulated HC_WIFI_F_* */
    uint32_t count;      /* number of sightings */
    uint64_t last_us;    /* esp_timer time of last sighting */
} hc_wifi_ap_t;

esp_err_t hc_wifi_recon_init(void);

/* Fold one sighting into the table (called by the mode manager's RX fan-out). */
void hc_wifi_recon_on_ap(const hc_wifi_ap_evt_t *ev);

/* Copy up to `cap` records into `out`, sorted strongest-RSSI first.
 * Returns the number written. Safe to call from the UI task. */
size_t hc_wifi_recon_snapshot(hc_wifi_ap_t *out, size_t cap);

size_t hc_wifi_recon_count(void);
void   hc_wifi_recon_clear(void);

#ifdef __cplusplus
}
#endif
#endif /* HC_WIFI_RECON_H */
