/*
 * Wasp Nest — P4 BLE recon table implementation
 */
#include "hc_ble_recon.h"

#include <string.h>
#include <stdlib.h>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "esp_timer.h"

#define HC_BLE_MAX 96

static hc_ble_dev_t      s_dev[HC_BLE_MAX];
static size_t            s_n;
static SemaphoreHandle_t s_lock;

esp_err_t hc_ble_recon_init(void)
{
    if (!s_lock) s_lock = xSemaphoreCreateMutex();
    s_n = 0;
    return s_lock ? ESP_OK : ESP_ERR_NO_MEM;
}

static int find_addr(const uint8_t *a)
{
    for (size_t i = 0; i < s_n; i++)
        if (memcmp(s_dev[i].addr, a, 6) == 0) return (int)i;
    return -1;
}

void hc_ble_recon_on_dev(const hc_ble_dev_evt_t *ev)
{
    if (!ev || !s_lock) return;
    xSemaphoreTake(s_lock, portMAX_DELAY);

    int idx = find_addr(ev->addr);
    if (idx < 0) {
        if (s_n >= HC_BLE_MAX) { xSemaphoreGive(s_lock); return; }
        idx = (int)s_n++;
        memset(&s_dev[idx], 0, sizeof(s_dev[idx]));
        memcpy(s_dev[idx].addr, ev->addr, 6);
        s_dev[idx].rssi = ev->rssi;
    }
    hc_ble_dev_t *d = &s_dev[idx];
    if (ev->rssi > d->rssi) d->rssi = ev->rssi;
    d->addr_type = ev->addr_type;
    d->flags    |= ev->flags;
    if (ev->name_len) {
        uint8_t n = ev->name_len > 29 ? 29 : ev->name_len;
        memcpy(d->name, ev->name, n);
        d->name[n]  = '\0';
        d->name_len = n;
    }
    d->count++;
    d->last_us = (uint64_t)esp_timer_get_time();

    xSemaphoreGive(s_lock);
}

static int cmp_rssi(const void *x, const void *y)
{
    const hc_ble_dev_t *a = x, *b = y;
    return (int)b->rssi - (int)a->rssi;
}

size_t hc_ble_recon_snapshot(hc_ble_dev_t *out, size_t cap)
{
    if (!out || !cap || !s_lock) return 0;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    size_t n = s_n < cap ? s_n : cap;
    memcpy(out, s_dev, n * sizeof(hc_ble_dev_t));
    xSemaphoreGive(s_lock);
    qsort(out, n, sizeof(hc_ble_dev_t), cmp_rssi);
    return n;
}

size_t hc_ble_recon_count(void) { return s_n; }

void hc_ble_recon_clear(void)
{
    if (!s_lock) return;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    s_n = 0;
    xSemaphoreGive(s_lock);
}
