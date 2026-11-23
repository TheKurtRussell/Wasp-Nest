/*
 * Wasp Nest — C6 BLE scanner (NimBLE observer)
 *
 * The BLE sibling of hc_radio_wifi_sniffer / hc_radio_sniffer. Brings up NimBLE
 * as an observer and reports each advertisement (address, RSSI, name, flags) via
 * a callback, which the mode agent forwards over the link to the P4. Recon only:
 * scan, no connect.
 *
 * STATUS: scaffold in the repo's style; compiles against ESP-IDF v5.5 NimBLE,
 * needs on-Tab5 iteration (NimBLE re-init across mode switches, coexistence).
 */
#ifndef HC_RADIO_BLE_SCANNER_H
#define HC_RADIO_BLE_SCANNER_H

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"
#include "hc_link_protocol.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Delivered from the NimBLE host task for each advertisement seen. */
typedef void (*hc_ble_dev_cb_t)(const hc_ble_dev_evt_t *dev, void *ctx);

typedef struct {
    hc_ble_dev_cb_t on_dev;
    void           *ctx;
} hc_ble_scanner_callbacks_t;

esp_err_t hc_ble_scanner_init(const hc_ble_scanner_callbacks_t *cb);
esp_err_t hc_ble_scanner_deinit(void);
esp_err_t hc_ble_scanner_start(const hc_blescan_cfg_t *cfg);
esp_err_t hc_ble_scanner_stop(void);
bool      hc_ble_scanner_is_running(void);

#ifdef __cplusplus
}
#endif
#endif /* HC_RADIO_BLE_SCANNER_H */
