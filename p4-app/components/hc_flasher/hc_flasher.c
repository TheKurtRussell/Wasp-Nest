/*
 * Wasp Nest — USB-host ESP flasher implementation
 *
 * Orchestration is real and complete: enter download mode -> sync -> detect ->
 * flash_begin/data/end, streaming image bytes from the SD card. The one piece
 * that must bind to hardware is the USB VCP transport (open the plugged-in
 * device and get read/write/line-control). That is isolated in the four
 * vcp_* functions below and is the single integration point to finish on-device.
 *
 * To wire the transport, add these managed components (idf_component.yml already
 * lists them) and open the device in vcp_open():
 *     espressif/usb_host_vcp, usb_host_cdc_acm, usb_host_cp210x_vcp,
 *     usb_host_ch34x_vcp, usb_host_ftdi_vcp
 * The usb_host_vcp "VCP::open_from_new_dev()" call returns a CdcAcmDevice-like
 * handle exposing tx_blocking()/register_rx + line_coding/control for DTR/RTS.
 */
#include "hc_flasher.h"
#include "hc_esp_loader.h"

#include <stdio.h>
#include <string.h>
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define TAG "hc_flash"
#define FLASH_BLOCK 1024

static hc_flash_progress_cb_t s_cb;
static void                  *s_ctx;
static bool                   s_usb_jtag_target;  /* set during detect */

/* ---------------- USB VCP transport (integration point) ----------------- */
/* These wrap the esp usb_host_vcp handle. Marked TODO where the real handle
 * calls go; the signatures match what hc_esp_loader expects. */
static int  vcp_write(const uint8_t *buf, size_t len, void *io) {
    (void)io; /* TODO: return vcp->tx_blocking(buf, len); */
    (void)buf; (void)len; return (int)len;
}
static int  vcp_read(uint8_t *buf, size_t len, uint32_t to_ms, void *io) {
    (void)io; (void)buf; (void)len; (void)to_ms;
    /* TODO: pull from the VCP rx ring with timeout; return bytes read. */
    return 0;
}
static void vcp_set_dtr_rts(bool dtr, bool rts, void *io) {
    (void)io; (void)dtr; (void)rts;
    /* TODO: vcp->line_control(dtr, rts); then a short vTaskDelay(2ms). */
}
static void vcp_set_baud(uint32_t baud, void *io) {
    (void)io; (void)baud;
    /* TODO: vcp->line_coding_set({baud,8,N,1}); */
}

static esp_err_t vcp_open(void) {
    /* TODO: usb_host install + VCP::open_from_new_dev(); detect bridge type and
     * set s_usb_jtag_target = (device is native USB-serial-jtag). */
    ESP_LOGW(TAG, "vcp_open(): USB host transport not yet bound (see hc_flasher.c)");
    return ESP_ERR_NOT_SUPPORTED;
}

static const hc_loader_transport_t k_transport = {
    .write = vcp_write, .read = vcp_read,
    .set_dtr_rts = vcp_set_dtr_rts, .set_baud = vcp_set_baud, .io = NULL,
};

/* ---------------- chip detect ------------------------------------------- */
/* CHIP_DETECT_MAGIC register (0x40001000) returns a per-target magic. */
#define CHIP_MAGIC_REG 0x40001000u
static hc_target_chip_t magic_to_chip(uint32_t m) {
    switch (m) {
    case 0x00f01d83: return HC_TGT_ESP32;
    case 0x000007c6: return HC_TGT_ESP32S2;   /* example values; verify per esptool */
    case 0x09: /* fallthrough placeholders */  return HC_TGT_ESP32S3;
    default: return HC_TGT_UNKNOWN;
    }
}

const char *hc_target_chip_name(hc_target_chip_t c) {
    switch (c) {
    case HC_TGT_ESP32:   return "ESP32";
    case HC_TGT_ESP32S2: return "ESP32-S2";
    case HC_TGT_ESP32S3: return "ESP32-S3";
    case HC_TGT_ESP32C3: return "ESP32-C3";
    case HC_TGT_ESP32C6: return "ESP32-C6";
    case HC_TGT_ESP32C5: return "ESP32-C5";
    case HC_TGT_ESP32H2: return "ESP32-H2";
    case HC_TGT_ESP32P4: return "ESP32-P4";
    default:             return "unknown";
    }
}

static void report(hc_flash_state_t st, hc_target_chip_t chip,
                   uint32_t done, uint32_t total, const char *msg) {
    if (!s_cb) return;
    hc_flash_progress_t p = { .state = st, .chip = chip,
                              .bytes_total = total, .bytes_done = done };
    strncpy(p.message, msg ? msg : "", sizeof(p.message) - 1);
    s_cb(&p, s_ctx);
}

/* ---------------- public API -------------------------------------------- */
esp_err_t hc_flasher_init(hc_flash_progress_cb_t cb, void *ctx) {
    s_cb = cb; s_ctx = ctx;
    hc_loader_bind(&k_transport);
    return ESP_OK;
}

esp_err_t hc_flasher_start(void) {
    esp_err_t e = vcp_open();
    if (e == ESP_OK) report(HC_FL_DEVICE_READY, HC_TGT_UNKNOWN, 0, 0, "device ready");
    return e;
}

esp_err_t hc_flasher_detect(hc_target_chip_t *chip_out) {
    hc_loader_enter_download_mode(s_usb_jtag_target);
    vTaskDelay(pdMS_TO_TICKS(100));
    if (hc_loader_sync() != ESP_OK) { report(HC_FL_ERROR, HC_TGT_UNKNOWN,0,0,"no sync"); return ESP_ERR_TIMEOUT; }

    uint32_t magic = 0;
    hc_loader_read_reg(CHIP_MAGIC_REG, &magic);
    hc_target_chip_t chip = magic_to_chip(magic);
    if (chip_out) *chip_out = chip;
    report(HC_FL_SYNCED, chip, 0, 0, hc_target_chip_name(chip));
    return ESP_OK;
}

esp_err_t hc_flasher_write(const hc_flash_region_t *regions, size_t n) {
    hc_target_chip_t chip;
    esp_err_t e = hc_flasher_detect(&chip);
    if (e != ESP_OK) return e;

    /* optional: SPI_ATTACH for targets that need it (safe no-op otherwise) */
    hc_loader_command(LDR_SPI_ATTACH, (const uint8_t[8]){0}, 8, 0, NULL, 1000);

    for (size_t i = 0; i < n; ++i) {
        FILE *f = fopen(regions[i].sd_path, "rb");
        if (!f) { report(HC_FL_ERROR, chip,0,0,"open img fail"); return ESP_FAIL; }
        fseek(f, 0, SEEK_END); long sz = ftell(f); fseek(f, 0, SEEK_SET);

        report(HC_FL_WRITING, chip, 0, sz, regions[i].sd_path);
        if (hc_loader_flash_begin((uint32_t)sz, regions[i].offset, FLASH_BLOCK) != ESP_OK) {
            fclose(f); report(HC_FL_ERROR, chip,0,sz,"flash_begin"); return ESP_FAIL;
        }

        uint8_t block[FLASH_BLOCK];
        uint32_t seq = 0, done = 0;
        size_t r;
        while ((r = fread(block, 1, FLASH_BLOCK, f)) > 0) {
            if (r < FLASH_BLOCK) memset(block + r, 0xFF, FLASH_BLOCK - r); /* pad */
            if (hc_loader_flash_block(block, FLASH_BLOCK, seq++) != ESP_OK) {
                fclose(f); report(HC_FL_ERROR, chip, done, sz, "flash_data"); return ESP_FAIL;
            }
            done += r;
            report(HC_FL_WRITING, chip, done, (uint32_t)sz, "writing");
        }
        fclose(f);
    }

    hc_loader_flash_end(true); /* reboot target */
    report(HC_FL_DONE, chip, 0, 0, "done");
    return ESP_OK;
}

/* ---------------- serial monitor ---------------------------------------- */
static hc_serial_line_cb_t s_mon_cb;
static void *s_mon_ctx;
static TaskHandle_t s_mon_task;
static volatile bool s_mon_run;

static void monitor_task(void *arg) {
    char line[256]; size_t n = 0; uint8_t b;
    while (s_mon_run) {
        if (vcp_read(&b, 1, 100, NULL) == 1) {
            if (b == '\n' || n == sizeof(line) - 1) {
                line[n] = 0; if (s_mon_cb) s_mon_cb(line, s_mon_ctx); n = 0;
            } else if (b != '\r') line[n++] = (char)b;
        }
    }
    vTaskDelete(NULL);
}

esp_err_t hc_flasher_monitor_start(uint32_t baud, hc_serial_line_cb_t cb, void *ctx) {
    vcp_set_baud(baud, NULL);
    s_mon_cb = cb; s_mon_ctx = ctx; s_mon_run = true;
    return xTaskCreate(monitor_task, "hc_mon", 4096, NULL, 8, &s_mon_task) == pdPASS
           ? ESP_OK : ESP_ERR_NO_MEM;
}
esp_err_t hc_flasher_monitor_stop(void) { s_mon_run = false; return ESP_OK; }
