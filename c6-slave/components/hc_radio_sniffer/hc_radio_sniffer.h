/*
 * Wasp Nest — C6 802.15.4 sniffer
 *
 * Wraps esp_ieee802154 in promiscuous mode and surfaces captured frames,
 * energy-scan results and active-scan (beacon) results through a single
 * callback, so hc_mode_agent can forward them over the link to the P4.
 */
#ifndef HC_RADIO_SNIFFER_H
#define HC_RADIO_SNIFFER_H

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"
#include "hc_link_protocol.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Called from a normal task context (NOT the radio ISR) for each captured
 * frame. `mpdu`/`len` is the raw MAC frame; meta carries channel/rssi/lqi. */
typedef void (*hc_sniffer_frame_cb_t)(const hc_frame_hdr_t *meta,
                                      const uint8_t *mpdu, void *ctx);

typedef void (*hc_sniffer_energy_cb_t)(const hc_energy_result_t *res, void *ctx);
typedef void (*hc_sniffer_scan_cb_t)(const hc_scan_result_t *res, void *ctx);

typedef struct {
    hc_sniffer_frame_cb_t  on_frame;
    hc_sniffer_energy_cb_t on_energy;
    hc_sniffer_scan_cb_t   on_scan;
    void                  *ctx;
} hc_sniffer_callbacks_t;

/* Bring up the radio driver + capture task. Call once when entering SNIFFER
 * mode. Safe to call esp_ieee802154_disable() via hc_sniffer_stop() before
 * handing the radio to another mode. */
esp_err_t hc_sniffer_init(const hc_sniffer_callbacks_t *cb);
esp_err_t hc_sniffer_deinit(void);

/* Start/stop continuous capture using the supplied config (fixed channel or
 * hop list). */
esp_err_t hc_sniffer_start(const hc_sniff_cfg_t *cfg);
esp_err_t hc_sniffer_stop(void);

/* Change channel mid-capture without a full restart (used by the hop task and
 * by the P4's manual channel control). */
esp_err_t hc_sniffer_set_channel(uint8_t channel);

/* Blocking scans (run on the sniffer task). Results are delivered via the
 * energy/scan callbacks; completion is signalled by the caller. */
esp_err_t hc_sniffer_energy_scan(const hc_energy_scan_t *req);
esp_err_t hc_sniffer_active_scan(const hc_active_scan_t *req);

bool hc_sniffer_is_running(void);

#ifdef __cplusplus
}
#endif
#endif /* HC_RADIO_SNIFFER_H */
