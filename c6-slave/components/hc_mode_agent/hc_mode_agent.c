/*
 * Wasp Nest — C6 mode agent implementation
 */
#include "hc_mode_agent.h"
#include "hc_link_slave.h"
#include "hc_radio_sniffer.h"
#include "hc_radio_wifi.h"
#include "hc_radio_wifi_sniffer.h"

#include <string.h>
#include "esp_log.h"
#include "esp_check.h"
#include "esp_system.h"
#include "esp_attr.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define TAG "hc_mode"

/* Survives a software reboot (not power loss) — used to come up directly in the
 * requested role after a mode switch that needs a restart. */
static RTC_NOINIT_ATTR uint32_t s_boot_mode_magic;
static RTC_NOINIT_ATTR uint8_t  s_boot_mode;
#define BOOT_MODE_MAGIC 0x484D4F44u /* 'HMOD' */

static hc_radio_mode_t s_mode = HC_MODE_IDLE;

/* ---- capture callbacks: forward to P4 ------------------------------------ */
static void on_frame(const hc_frame_hdr_t *meta, const uint8_t *mpdu, void *ctx)
{
    /* Pack hdr + raw mpdu into one link message. */
    uint8_t buf[sizeof(hc_frame_hdr_t) + 128];
    memcpy(buf, meta, sizeof(*meta));
    memcpy(buf + sizeof(*meta), mpdu, meta->len);
    hc_link_slave_send(HC_EV_FRAME, buf, sizeof(*meta) + meta->len);
}
static void on_energy(const hc_energy_result_t *res, void *ctx)
{
    hc_link_slave_send(HC_EV_ENERGY, res, sizeof(*res));
}
static void on_scan(const hc_scan_result_t *res, void *ctx)
{
    hc_link_slave_send(HC_EV_SCAN_RESULT, res, sizeof(*res));
}

static const hc_sniffer_callbacks_t k_sniff_cb = {
    .on_frame = on_frame, .on_energy = on_energy, .on_scan = on_scan, .ctx = NULL,
};

/* Wi-Fi recon: forward each AP/probe sighting to the P4. Raw frames (on_frame)
 * are left off in this base — turn them on with a larger pack buffer + DLT 105
 * when you want pcap. */
static void on_wifi_ap(const hc_wifi_ap_evt_t *ap, void *ctx)
{
    hc_link_slave_send(HC_EV_WIFI_AP, ap, sizeof(*ap));
}

static const hc_wifi_sniffer_callbacks_t k_wifi_cb = {
    .on_ap = on_wifi_ap, .on_frame = NULL, .ctx = NULL,
};

/* ---- mode transitions ---------------------------------------------------- */
static void report_mode(uint8_t ok, uint8_t reason)
{
    hc_mode_state_t st = { .mode = s_mode, .ok = ok, .reason = reason };
    hc_link_slave_send(HC_EV_MODE, &st, sizeof(st));
}

static esp_err_t enter_sniffer(void)
{
    esp_err_t e = hc_sniffer_init(&k_sniff_cb);
    if (e != ESP_OK) return e;
    s_mode = HC_MODE_SNIFFER;
    return ESP_OK;
}

static esp_err_t enter_wifi_sniffer(void)
{
    esp_err_t e = hc_wifi_sniffer_init(&k_wifi_cb);
    if (e != ESP_OK) return e;
    s_mode = HC_MODE_WIFI_SNIFFER;
    return ESP_OK;
}

static void enter_wifi_via_reboot(void)
{
    /* Persist intent and restart into the esp_hosted slave role. */
    s_boot_mode_magic = BOOT_MODE_MAGIC;
    s_boot_mode = HC_MODE_WIFI;
    hc_link_slave_logf(2, "switching to WIFI mode, rebooting C6");
    vTaskDelay(pdMS_TO_TICKS(50));
    esp_restart();
}

static void apply_mode(hc_radio_mode_t target)
{
    if (target == s_mode) { report_mode(1, HC_MODEFAIL_NONE); return; }

    /* leave current */
    if (s_mode == HC_MODE_SNIFFER)      hc_sniffer_deinit();
    if (s_mode == HC_MODE_WIFI_SNIFFER) hc_wifi_sniffer_deinit();

    switch (target) {
    case HC_MODE_IDLE:
        s_mode = HC_MODE_IDLE;
        report_mode(1, HC_MODEFAIL_NONE);
        break;
    case HC_MODE_SNIFFER:
        if (enter_sniffer() == ESP_OK) report_mode(1, HC_MODEFAIL_NONE);
        else                           report_mode(0, HC_MODEFAIL_RADIO_ERR);
        break;
    case HC_MODE_WIFI_SNIFFER:
        if (enter_wifi_sniffer() == ESP_OK) report_mode(1, HC_MODEFAIL_NONE);
        else                                report_mode(0, HC_MODEFAIL_RADIO_ERR);
        break;
    case HC_MODE_WIFI:
        report_mode(1, HC_MODEFAIL_NONE); /* ack before we reboot */
        enter_wifi_via_reboot();          /* does not return */
        break;
    default:
        report_mode(0, HC_MODEFAIL_UNSUPPORTED);
        break;
    }
}

/* ---- link command handler ------------------------------------------------ */
static void on_link_rx(hc_opcode_t op, const uint8_t *p, uint16_t len, void *ctx)
{
    switch (op) {
    case HC_OP_HELLO: {
        hc_hello_t h = { .proto_version = HC_PROTO_VERSION, .chip = 1,
                         .fw_build = 0 };
        strncpy(h.fw_semver, "0.1.0", sizeof(h.fw_semver));
        hc_link_slave_send(HC_EV_HELLO, &h, sizeof(h));
        break;
    }
    case HC_OP_PING:
        hc_link_slave_send(HC_EV_PONG, NULL, 0);
        break;
    case HC_OP_SET_MODE:
        if (len >= sizeof(hc_set_mode_t))
            apply_mode((hc_radio_mode_t)((const hc_set_mode_t *)p)->mode);
        break;
    case HC_OP_SNIFF_START:
        if (s_mode == HC_MODE_SNIFFER && len >= sizeof(hc_sniff_cfg_t))
            hc_sniffer_start((const hc_sniff_cfg_t *)p);
        break;
    case HC_OP_SNIFF_STOP:
        hc_sniffer_stop();
        break;
    case HC_OP_SNIFF_SET_CHAN:
        if (len >= sizeof(hc_set_chan_t))
            hc_sniffer_set_channel(((const hc_set_chan_t *)p)->channel);
        break;
    case HC_OP_SCAN_ENERGY:
        if (len >= sizeof(hc_energy_scan_t)) {
            hc_sniffer_energy_scan((const hc_energy_scan_t *)p);
            hc_link_slave_send(HC_EV_SCAN_DONE, NULL, 0);
        }
        break;
    case HC_OP_SCAN_ACTIVE:
        if (len >= sizeof(hc_active_scan_t)) {
            hc_sniffer_active_scan((const hc_active_scan_t *)p);
            hc_link_slave_send(HC_EV_SCAN_DONE, NULL, 0);
        }
        break;
    case HC_OP_WIFI_ENTER:
        apply_mode(HC_MODE_WIFI);
        break;
    case HC_OP_WSCAN_START:
        if (s_mode == HC_MODE_WIFI_SNIFFER && len >= sizeof(hc_wscan_cfg_t))
            hc_wifi_sniffer_start((const hc_wscan_cfg_t *)p);
        break;
    case HC_OP_WSCAN_STOP:
        hc_wifi_sniffer_stop();
        break;
    case HC_OP_WSCAN_SET_CHAN:
        if (len >= sizeof(hc_set_chan_t))
            hc_wifi_sniffer_set_channel(((const hc_set_chan_t *)p)->channel);
        break;
    default:
        hc_link_slave_logf(1, "unknown op 0x%02x", op);
        break;
    }
}

esp_err_t hc_mode_agent_start(void)
{
    /* If we rebooted specifically to enter Wi-Fi mode, do that before anything
     * else and never return (esp_hosted slave takes over). */
    if (s_boot_mode_magic == BOOT_MODE_MAGIC && s_boot_mode == HC_MODE_WIFI) {
        s_boot_mode_magic = 0;
        ESP_LOGI(TAG, "cold-boot into WIFI (esp_hosted slave)");
        return hc_radio_wifi_run_hosted_slave(); /* blocks */
    }

    ESP_RETURN_ON_ERROR(hc_link_slave_init(on_link_rx, NULL), TAG, "link");
    ESP_LOGI(TAG, "mode agent up, idle");
    /* Announce ourselves so the P4 can match proto versions. */
    on_link_rx(HC_OP_HELLO, NULL, 0, NULL);
    return ESP_OK;
}

hc_radio_mode_t hc_mode_agent_current(void) { return s_mode; }
