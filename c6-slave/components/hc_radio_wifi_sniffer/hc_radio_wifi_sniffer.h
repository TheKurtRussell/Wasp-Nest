/*
 * Wasp Nest — C6 Wi-Fi recon sniffer
 *
 * The Wi-Fi sibling of hc_radio_sniffer. Puts esp_wifi into WIFI_MODE_NULL +
 * promiscuous, filters to management frames, parses beacon / probe-response /
 * probe-request into compact AP sightings, and surfaces them (plus optional raw
 * frames for pcap) through callbacks so hc_mode_agent can forward them over the
 * existing link to the P4.
 *
 * This runs esp_wifi LOCALLY on the C6. It does NOT use esp_hosted, so it is a
 * soft mode switch (no reboot), exactly like the 802.15.4 sniffer. The esp_hosted
 * slave (hc_radio_wifi, HC_MODE_WIFI) stays a separate, reboot-based mode.
 *
 * STATUS: scaffold, matching the repo's "clean interfaces, bring-up TODOs"
 * convention. Compiles against ESP-IDF v5.5 esp_wifi; needs on-Tab5 iteration
 * (promiscuous RX load, channel-hop timing). Recon only — no TX, no deauth.
 */
#ifndef HC_RADIO_WIFI_SNIFFER_H
#define HC_RADIO_WIFI_SNIFFER_H

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"
#include "hc_link_protocol.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Delivered from a normal task context (NOT the Wi-Fi RX ISR path) for each
 * AP/probe sighting. The P4 dedupes by BSSID. */
typedef void (*hc_wifi_ap_cb_t)(const hc_wifi_ap_evt_t *ap, void *ctx);

/* Optional: raw 802.11 MPDU for pcap capture (only if cfg.want_raw). `meta`
 * reuses hc_frame_hdr_t (ts/channel/rssi/fcs_ok/len). */
typedef void (*hc_wifi_frame_cb_t)(const hc_frame_hdr_t *meta,
                                   const uint8_t *mpdu, void *ctx);

typedef struct {
    hc_wifi_ap_cb_t    on_ap;
    hc_wifi_frame_cb_t on_frame;   /* may be NULL */
    void              *ctx;
} hc_wifi_sniffer_callbacks_t;

/* Bring up / tear down the esp_wifi driver in promiscuous recon mode. Call
 * hc_wifi_sniffer_init when entering HC_MODE_WIFI_SNIFFER; deinit before handing
 * the radio to another mode. */
esp_err_t hc_wifi_sniffer_init(const hc_wifi_sniffer_callbacks_t *cb);
esp_err_t hc_wifi_sniffer_deinit(void);

/* Start/stop continuous capture using the supplied config (fixed channel or
 * hop list over 2.4 GHz). */
esp_err_t hc_wifi_sniffer_start(const hc_wscan_cfg_t *cfg);
esp_err_t hc_wifi_sniffer_stop(void);

/* Park on one channel mid-capture (manual control / forager coordination). */
esp_err_t hc_wifi_sniffer_set_channel(uint8_t channel);

bool hc_wifi_sniffer_is_running(void);

#ifdef __cplusplus
}
#endif
#endif /* HC_RADIO_WIFI_SNIFFER_H */
