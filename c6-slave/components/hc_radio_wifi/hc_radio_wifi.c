/*
 * Wasp Nest — C6 Wi-Fi mode (esp_hosted slave) — integration stub
 * See header for the two supported ways to make this real.
 */
#include "hc_radio_wifi.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define TAG "hc_wifi"

esp_err_t hc_radio_wifi_run_hosted_slave(void)
{
    ESP_LOGW(TAG, "WIFI mode requested — esp_hosted slave not yet linked in.");
    ESP_LOGW(TAG, "See hc_radio_wifi.h: ship the stock esp_hosted slave (option A/B).");
    /* Park so the device is in a known state until phase-2 wires this up. */
    for (;;) vTaskDelay(pdMS_TO_TICKS(1000));
    return ESP_OK;
}
