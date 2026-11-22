/*
 * Wasp Nest — C6 Wi-Fi mode (esp_hosted slave)
 *
 * In Wi-Fi mode the C6 is a plain esp_hosted co-processor: the P4 runs the
 * Wi-Fi/LwIP stack via esp_wifi_remote and the C6 just shuttles packets over
 * SDIO. We do NOT reimplement that — we hand off to the stock esp_hosted slave.
 *
 * STATUS: integration point. The hosted slave is normally built as its OWN
 * firmware image from the esp_hosted "slave" example. Two supported ways to
 * ship it (pick one in docs/roadmap.md):
 *
 *   (A) Separate image + reboot (what the mode agent assumes today):
 *       Flash the stock esp_hosted slave as a second OTA app slot. On a WIFI
 *       request we esp_restart() into that slot. Cleanest; least custom code.
 *
 *   (B) One combined image:
 *       Pull esp_hosted's slave sources into this component and call its entry
 *       from hc_radio_wifi_run_hosted_slave(). More work, single image.
 *
 * For now this function logs and parks so the project links and the sniffer
 * path is fully testable. Wiring up (A) or (B) is a tracked phase-2 task.
 */
#ifndef HC_RADIO_WIFI_H
#define HC_RADIO_WIFI_H

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Runs the esp_hosted slave role. Blocks (does not return) in a real build. */
esp_err_t hc_radio_wifi_run_hosted_slave(void);

#ifdef __cplusplus
}
#endif
#endif /* HC_RADIO_WIFI_H */
