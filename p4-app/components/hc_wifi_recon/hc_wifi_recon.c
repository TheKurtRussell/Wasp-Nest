/*
 * Wasp Nest — P4 Wi-Fi recon table implementation
 */
#include "hc_wifi_recon.h"

#include <string.h>
#include <stdlib.h>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "esp_timer.h"

#define HC_WIFI_MAX 64

static hc_wifi_ap_t      s_aps[HC_WIFI_MAX];
static size_t            s_n;
static SemaphoreHandle_t s_lock;

esp_err_t hc_wifi_recon_init(void)
{
    if (!s_lock) s_lock = xSemaphoreCreateMutex();
    s_n = 0;
    return s_lock ? ESP_OK : ESP_ERR_NO_MEM;
}

static int find_bssid(const uint8_t *b)
{
    for (size_t i = 0; i < s_n; i++)
        if (memcmp(s_aps[i].bssid, b, 6) == 0) return (int)i;
    return -1;
}

void hc_wifi_recon_on_ap(const hc_wifi_ap_evt_t *ev)
{
    if (!ev || !s_lock) return;
    xSemaphoreTake(s_lock, portMAX_DELAY);

    int idx = find_bssid(ev->bssid);
    if (idx < 0) {
        if (s_n >= HC_WIFI_MAX) { xSemaphoreGive(s_lock); return; }
        idx = (int)s_n++;
        memset(&s_aps[idx], 0, sizeof(s_aps[idx]));
        memcpy(s_aps[idx].bssid, ev->bssid, 6);
        s_aps[idx].rssi = ev->rssi;
    }
    hc_wifi_ap_t *a = &s_aps[idx];
    if (ev->rssi > a->rssi) a->rssi = ev->rssi;   /* keep the strongest */
    a->channel = ev->channel;
    a->flags  |= ev->flags;
    if (ev->ssid_len) {                           /* a probe may lack the SSID */
        uint8_t n = ev->ssid_len > 32 ? 32 : ev->ssid_len;
        memcpy(a->ssid, ev->ssid, n);
        a->ssid[n]  = '\0';
        a->ssid_len = n;
    }
    a->count++;
    a->last_us = (uint64_t)esp_timer_get_time();

    xSemaphoreGive(s_lock);
}

static int cmp_rssi(const void *x, const void *y)
{
    const hc_wifi_ap_t *a = x, *b = y;
    return (int)b->rssi - (int)a->rssi;           /* descending */
}

size_t hc_wifi_recon_snapshot(hc_wifi_ap_t *out, size_t cap)
{
    if (!out || !cap || !s_lock) return 0;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    size_t n = s_n < cap ? s_n : cap;
    memcpy(out, s_aps, n * sizeof(hc_wifi_ap_t));
    xSemaphoreGive(s_lock);
    qsort(out, n, sizeof(hc_wifi_ap_t), cmp_rssi);
    return n;
}

size_t hc_wifi_recon_count(void) { return s_n; }

void hc_wifi_recon_clear(void)
{
    if (!s_lock) return;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    s_n = 0;
    xSemaphoreGive(s_lock);
}
