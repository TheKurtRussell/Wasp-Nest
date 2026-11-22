/*
 * Wasp Nest — C6 updater implementation (esp-serial-flasher over SDIO)
 *
 * ┌─ STATUS: DRAFT, NOT YET ENABLED ──────────────────────────────────────┐
 * │ esp-serial-flasher is intentionally still OUT of main/idf_component.yml │
 * │ so the project keeps building green. That makes HAVE_SERIAL_FLASHER 0,  │
 * │ and the real flashing body below is compiled out (the stub path runs).  │
 * │ To turn it on (next co-working session, with live compile feedback):    │
 * │   1. add `espressif/esp-serial-flasher: "^2.1.0"` to main/idf_component  │
 * │   2. add `esp-serial-flasher` to this component's REQUIRES               │
 * │   3. confirm the v2 handle-API names + SDIO config struct fields against │
 * │      the pinned version's port/esp32_port.h (marked TODO(api) below)     │
 * │   4. build, and run the BOOT-pin test (see docs/flashing.md, Plan A)     │
 * └────────────────────────────────────────────────────────────────────────┘
 *
 * Why SDIO (not UART): on the Tab5 the P4 and C6 are wired together ONLY over
 * the SDIO2 bus (+ RESET/IO2); there is no P4<->C6 UART. esp-serial-flasher
 * supports exactly ESP32-P4 host -> ESP32-C6 target over SDIO.
 *
 * Download mode: the library resets the C6 while holding its BOOT strap to enter
 * ROM download. We drive RESET=HC_PIN_C6_RESET and BOOT=HC_PIN_C6_IO2. Whether
 * IO2 is really the C6 boot strap on this board is THE open question — if
 * esp_loader_connect() fails, that wiring is wrong and the guaranteed fallback
 * is M5's USB-TTL header route (docs/flashing.md, Plan B).
 */
#include "hc_c6updater.h"
#include "hc_board.h"

#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#if __has_include("esp_loader.h")
#include "esp_loader.h"
#include "esp32_sdio_port.h"   /* esp32_sdio_port_t, esp32_sdio_ops, SDIO_4BIT */
#define HAVE_SERIAL_FLASHER 1
#else
#define HAVE_SERIAL_FLASHER 0
#endif

#define TAG "hc_c6upd"
#define FLASH_CHUNK 1024

/* The web flasher downloads "merged-c6.bin"; docs also allow renaming to
 * "c6_service.bin". Accept either so the user can't trivially get it wrong. */
static const char *k_candidates[] = {
    "/sdcard/waspnest/c6_service.bin",
    "/sdcard/waspnest/merged-c6.bin",
};

static const char *find_image(const char *override_path)
{
    static char found[128];
    if (override_path) {
        struct stat st;
        if (stat(override_path, &st) == 0) { strncpy(found, override_path, sizeof(found)-1); return found; }
        return NULL;
    }
    for (size_t i = 0; i < sizeof(k_candidates)/sizeof(k_candidates[0]); ++i) {
        struct stat st;
        if (stat(k_candidates[i], &st) == 0) {
            strncpy(found, k_candidates[i], sizeof(found)-1);
            return found;
        }
    }
    return NULL;
}

bool hc_c6updater_should_offer(bool c6_handshake_ok, const char *sd_path)
{
    if (c6_handshake_ok) return false;       /* C6 already answers */
    return find_image(sd_path) != NULL;      /* an image is on the card */
}

esp_err_t hc_c6updater_run(const char *sd_path, hc_c6upd_progress_cb_t cb, void *ctx)
{
    const char *path = find_image(sd_path);
    if (!path) { if (cb) cb(-1, "no C6 image on SD", ctx); return ESP_ERR_NOT_FOUND; }

#if !HAVE_SERIAL_FLASHER
    ESP_LOGW(TAG, "esp-serial-flasher not enabled — see header to turn on.");
    ESP_LOGW(TAG, "Found image at %s but cannot flash C6 yet.", path);
    if (cb) cb(-1, "flasher disabled (draft)", ctx);
    return ESP_ERR_NOT_SUPPORTED;
#else
    FILE *f = fopen(path, "rb");
    if (!f) { if (cb) cb(-1, "open image failed", ctx); return ESP_FAIL; }
    fseek(f, 0, SEEK_END); long size = ftell(f); fseek(f, 0, SEEK_SET);
    ESP_LOGI(TAG, "flashing C6 from %s (%ld bytes) over SDIO", path, size);
    if (cb) cb(0, "connecting to C6 (SDIO)", ctx);

    /* Real esp-serial-flasher v2 SDIO port (from esp32_sdio_port.h). */
    esp_loader_t loader;
    esp32_sdio_port_t sdio = {
        .port.ops     = &esp32_sdio_ops,
        .slot         = SDMMC_HOST_SLOT_1,
        .max_freq_khz = 400,                 /* slow for download bring-up */
        .sdio_clk_pin = HC_PIN_C6_SDIO_CLK,  /* G12 */
        .sdio_cmd_pin = HC_PIN_C6_SDIO_CMD,  /* G13 */
        .sdio_d0_pin  = HC_PIN_C6_SDIO_D0,   /* G11 */
        .sdio_d1_pin  = HC_PIN_C6_SDIO_D1,   /* G10 */
        .sdio_d2_pin  = HC_PIN_C6_SDIO_D2,   /* G9  */
        .sdio_d3_pin  = HC_PIN_C6_SDIO_D3,   /* G8  */
        .reset_pin    = HC_PIN_C6_RESET,     /* G15 */
        .boot_pin     = HC_PIN_C6_IO2,       /* G14 — TEST: C6 boot strap candidate */
        .bus_width    = SDIO_4BIT,
    };
    if (esp_loader_init_sdio(&loader, &sdio.port) != ESP_LOADER_SUCCESS) {
        fclose(f); if (cb) cb(-1, "SDIO init failed", ctx); return ESP_FAIL;
    }

    esp_loader_connect_args_t connect = ESP_LOADER_CONNECT_DEFAULT();
    if (esp_loader_connect(&loader, &connect) != ESP_LOADER_SUCCESS) {
        /* Most likely the BOOT pin is wrong -> can't enter download mode. */
        esp_loader_deinit(&loader); fclose(f);
        ESP_LOGE(TAG, "C6 connect failed — BOOT pin (IO2/G14) likely not the strap.");
        ESP_LOGE(TAG, "Use the USB-TTL route instead (docs/flashing.md Plan B).");
        if (cb) cb(-1, "C6 connect failed (see Plan B)", ctx);
        return ESP_FAIL;
    }
    ESP_LOGI(TAG, "connected to C6");
    if (cb) cb(5, "connected, writing", ctx);

    /* offset + image_size must be 4-byte aligned (esp_loader_flash_cfg_t). */
    uint32_t aligned = ((uint32_t)size + 3u) & ~3u;
    esp_loader_flash_cfg_t fcfg = {
        .offset = 0, .image_size = aligned, .block_size = FLASH_CHUNK,
    };
    esp_err_t ret = ESP_OK;
    if (esp_loader_flash_start(&loader, &fcfg) != ESP_LOADER_SUCCESS) {
        ret = ESP_FAIL; if (cb) cb(-1, "flash_start failed", ctx);
    } else {
        uint8_t buf[FLASH_CHUNK]; long done = 0; size_t r;
        while ((r = fread(buf, 1, sizeof(buf), f)) > 0) {
            uint32_t wlen = (uint32_t)r;
            while (wlen & 3u) buf[wlen++] = 0xFF;   /* pad last block to 4 bytes */
            if (esp_loader_flash_write(&loader, &fcfg, buf, wlen) != ESP_LOADER_SUCCESS) {
                ret = ESP_FAIL; if (cb) cb(-1, "flash_write failed", ctx); break;
            }
            done += r;
            if (cb) cb((int)(100 * done / size), "writing", ctx);
        }
        if (ret == ESP_OK) esp_loader_flash_finish(&loader, &fcfg);
    }

    esp_loader_reset_target(&loader);   /* boot the C6 into the new app */
    esp_loader_deinit(&loader);         /* release the SDIO bus for the link */
    fclose(f);
    if (ret == ESP_OK && cb) cb(100, "C6 updated — reconnect over link", ctx);
    vTaskDelay(pdMS_TO_TICKS(300));
    return ret;
#endif
}
