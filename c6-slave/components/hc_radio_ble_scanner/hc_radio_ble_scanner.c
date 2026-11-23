/*
 * Wasp Nest — C6 BLE scanner (NimBLE observer) implementation
 */
#include "hc_radio_ble_scanner.h"

#include <string.h>
#include "esp_log.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "nimble/hci_common.h"
#include "host/ble_hs.h"
#include "host/util/util.h"

static const char *TAG = "hc_ble";

static hc_ble_scanner_callbacks_t s_cb;
static hc_blescan_cfg_t  s_cfg;
static volatile bool     s_running;    /* scan wanted */
static volatile bool     s_synced;     /* NimBLE stack ready */
static volatile bool     s_inited;
static uint8_t           s_own_addr_type;

static int gap_event(struct ble_gap_event *event, void *arg);

static void start_disc(void)
{
    if (!s_synced || !s_running) return;
    struct ble_gap_disc_params p = {0};
    p.passive           = s_cfg.active ? 0 : 1;
    p.filter_duplicates = 0;              /* we dedupe on the P4, keep RSSI fresh */
    p.itvl              = s_cfg.itvl;     /* 0 => controller default */
    p.window            = s_cfg.window;
    int rc = ble_gap_disc(s_own_addr_type, BLE_HS_FOREVER, &p, gap_event, NULL);
    if (rc != 0) ESP_LOGW(TAG, "ble_gap_disc rc=%d", rc);
}

static void on_sync(void)
{
    (void)ble_hs_util_ensure_addr(0);
    int rc = ble_hs_id_infer_auto(0, &s_own_addr_type);
    if (rc != 0) { ESP_LOGW(TAG, "infer_auto rc=%d", rc); s_own_addr_type = 0; }
    s_synced = true;
    start_disc();
}

static void on_reset(int reason)
{
    s_synced = false;
    ESP_LOGW(TAG, "nimble reset; reason=%d", reason);
}

static void host_task(void *param)
{
    (void)param;
    nimble_port_run();                 /* blocks until nimble_port_stop() */
    nimble_port_freertos_deinit();
}

static int gap_event(struct ble_gap_event *event, void *arg)
{
    (void)arg;
    if (event->type != BLE_GAP_EVENT_DISC) return 0;
    const struct ble_gap_disc_desc *d = &event->disc;

    hc_ble_dev_evt_t ev;
    memset(&ev, 0, sizeof(ev));
    memcpy(ev.addr, d->addr.val, 6);
    ev.addr_type = d->addr.type;
    ev.rssi      = d->rssi;

    if (d->addr.type == BLE_ADDR_RANDOM || d->addr.type == BLE_ADDR_RANDOM_ID)
        ev.flags |= HC_BLE_F_RANDOM;
    if (d->event_type == BLE_HCI_ADV_RPT_EVTYPE_ADV_IND ||
        d->event_type == BLE_HCI_ADV_RPT_EVTYPE_DIR_IND)
        ev.flags |= HC_BLE_F_CONNECTABLE;

    struct ble_hs_adv_fields f;
    if (ble_hs_adv_parse_fields(&f, d->data, d->length_data) == 0) {
        if (f.name != NULL && f.name_len > 0) {
            uint8_t n = f.name_len > sizeof(ev.name) ? sizeof(ev.name) : f.name_len;
            memcpy(ev.name, f.name, n);
            ev.name_len = n;
            ev.flags |= HC_BLE_F_NAMED;
        }
        if (f.mfg_data != NULL && f.mfg_data_len >= 2 &&
            f.mfg_data[0] == 0x4C && f.mfg_data[1] == 0x00)   /* Apple company id */
            ev.flags |= HC_BLE_F_APPLE;
    }

    if (s_cb.on_dev) s_cb.on_dev(&ev, s_cb.ctx);
    return 0;
}

esp_err_t hc_ble_scanner_init(const hc_ble_scanner_callbacks_t *cb)
{
    if (!cb) return ESP_ERR_INVALID_ARG;
    s_cb = *cb;
    if (s_inited) return ESP_OK;

    esp_err_t ret = nimble_port_init();
    if (ret != ESP_OK) { ESP_LOGE(TAG, "nimble_port_init: %d", ret); return ret; }

    ble_hs_cfg.sync_cb  = on_sync;
    ble_hs_cfg.reset_cb = on_reset;
    nimble_port_freertos_init(host_task);

    s_inited = true;
    ESP_LOGI(TAG, "nimble observer up");
    return ESP_OK;
}

esp_err_t hc_ble_scanner_start(const hc_blescan_cfg_t *cfg)
{
    if (!cfg) return ESP_ERR_INVALID_ARG;
    s_cfg     = *cfg;
    s_running = true;
    start_disc();                        /* or deferred to on_sync if not ready */
    return ESP_OK;
}

esp_err_t hc_ble_scanner_stop(void)
{
    s_running = false;
    if (s_synced) ble_gap_disc_cancel();
    return ESP_OK;
}

esp_err_t hc_ble_scanner_deinit(void)
{
    hc_ble_scanner_stop();
    if (s_inited) {
        nimble_port_stop();              /* unblocks host_task -> freertos_deinit */
        nimble_port_deinit();
        s_inited = false;
        s_synced = false;
    }
    return ESP_OK;
}

bool hc_ble_scanner_is_running(void) { return s_running; }
