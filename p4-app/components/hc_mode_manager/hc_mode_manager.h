/*
 * Wasp Nest — P4 mode manager
 *
 * The P4-side counterpart to the C6 mode agent. It:
 *   - owns the link, does the HELLO handshake,
 *   - drives the C6 between IDLE / SNIFFER / WIFI,
 *   - fans out capture/scan events to hc_explorer,
 *   - owns the C6 RESET line for the reboot-based WIFI switch.
 *
 * The UI talks ONLY to this module for radio control; it never touches the link
 * directly.
 */
#ifndef HC_MODE_MANAGER_H
#define HC_MODE_MANAGER_H

#include "esp_err.h"
#include "hc_link_protocol.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*hc_mm_mode_cb_t)(hc_radio_mode_t mode, bool ok, uint8_t reason, void *ctx);

esp_err_t hc_mode_manager_start(hc_mm_mode_cb_t on_mode, void *ctx);

/* Request a radio mode. For WIFI this triggers the C6 reboot sequence. */
esp_err_t hc_mode_manager_set_mode(hc_radio_mode_t mode);
hc_radio_mode_t hc_mode_manager_mode(void);

/* Sniffer control (valid only in SNIFFER mode). */
esp_err_t hc_mode_manager_sniff_start(const hc_sniff_cfg_t *cfg);
esp_err_t hc_mode_manager_sniff_stop(void);
esp_err_t hc_mode_manager_set_channel(uint8_t channel);
esp_err_t hc_mode_manager_energy_scan(uint32_t channel_mask, uint16_t dwell_ms);
esp_err_t hc_mode_manager_active_scan(uint32_t channel_mask, uint16_t dwell_ms);

/* Wi-Fi recon control (valid only in HC_MODE_WIFI_SNIFFER). AP/probe sightings
 * arrive as HC_EV_WIFI_AP; in this base they are logged (see hc_mode_manager.c).
 * Route them to hc_wifi_recon + the Wi-Fi UI tab next. */
esp_err_t hc_mode_manager_wifi_scan_start(const hc_wscan_cfg_t *cfg);
esp_err_t hc_mode_manager_wifi_scan_stop(void);
esp_err_t hc_mode_manager_wifi_set_channel(uint8_t channel);

/* BLE recon control (valid only in HC_MODE_BLE_SCANNER). Device sightings arrive
 * as HC_EV_BLE_DEV and are folded into hc_ble_recon. */
esp_err_t hc_mode_manager_ble_scan_start(const hc_blescan_cfg_t *cfg);
esp_err_t hc_mode_manager_ble_scan_stop(void);

/* Pulse the C6 reset line (recovery / forced role reboot). */
void hc_mode_manager_reset_c6(void);

#ifdef __cplusplus
}
#endif
#endif /* HC_MODE_MANAGER_H */
