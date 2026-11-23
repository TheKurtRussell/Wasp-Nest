/*
 * Wasp Nest — P4 BLE recon table
 *
 * BLE counterpart to hc_wifi_recon: dedupes HC_EV_BLE_DEV sightings by address
 * into a thread-safe table the UI reads to draw a live device list. Recon only.
 */
#ifndef HC_BLE_RECON_H
#define HC_BLE_RECON_H

#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"
#include "hc_link_protocol.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint8_t  addr[6];
    uint8_t  addr_type;
    char     name[30];   /* NUL-terminated; empty if none advertised */
    uint8_t  name_len;
    int8_t   rssi;       /* strongest seen */
    uint8_t  flags;      /* accumulated HC_BLE_F_* */
    uint32_t count;      /* sightings */
    uint64_t last_us;
} hc_ble_dev_t;

esp_err_t hc_ble_recon_init(void);
void      hc_ble_recon_on_dev(const hc_ble_dev_evt_t *ev);
size_t    hc_ble_recon_snapshot(hc_ble_dev_t *out, size_t cap);  /* strongest first */
size_t    hc_ble_recon_count(void);
void      hc_ble_recon_clear(void);

#ifdef __cplusplus
}
#endif
#endif /* HC_BLE_RECON_H */
