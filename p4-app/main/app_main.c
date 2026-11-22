/*
 * Wasp Nest — P4 application entry point
 *
 * Wires the subsystems together:
 *   board -> storage -> explorer(+UI sinks) -> mode manager(link+C6) -> flasher -> UI
 *
 * Everything is callback-driven: the C6 pushes frames up through the mode
 * manager into the explorer, which dissects and forwards to the UI and (when
 * capturing) to the pcap writer on SD.
 */
#include "esp_log.h"
#include "nvs_flash.h"

#include "hc_board.h"
#include "hc_storage.h"
#include "hc_explorer.h"
#include "hc_wifi_recon.h"
#include "hc_mode_manager.h"
#include "hc_flasher.h"
#include "hc_c6updater.h"
#include "hc_ui.h"

#define TAG "waspnest"

/* explorer -> UI */
static void on_row(const hc_dissected_t *row, void *ctx)    { hc_ui_add_frame(row); }
static void on_energy(const hc_energy_result_t *e, void *c) { hc_ui_set_energy(e); }
static void on_scan(const hc_scan_result_t *s, void *c)     { hc_ui_add_scan(s); }

/* mode manager -> UI */
static void on_mode(hc_radio_mode_t m, bool ok, uint8_t reason, void *c)
{
    ESP_LOGI(TAG, "mode -> %d ok=%d", m, ok);
    hc_ui_set_mode(m, ok);
}

/* flasher -> UI */
static void on_flash(const hc_flash_progress_t *p, void *c) { hc_ui_set_flash_progress(p); }

void app_main(void)
{
    ESP_LOGI(TAG, "Wasp Nest P4 app 0.1.0");
    ESP_ERROR_CHECK(nvs_flash_init());

    ESP_ERROR_CHECK(hc_board_init());        /* display BSP (headless until bound) */
    hc_storage_mount();                      /* SD; non-fatal if absent */

    hc_ui_init(hc_board_lvgl_display());     /* bound to the BSP display */

    hc_explorer_callbacks_t ecb = { .on_row = on_row, .on_energy = on_energy,
                                    .on_scan = on_scan };
    ESP_ERROR_CHECK(hc_explorer_init(&ecb));
    ESP_ERROR_CHECK(hc_wifi_recon_init());   /* Wi-Fi AP table; UI reads it live */

    hc_flasher_init(on_flash, NULL);
    /* hc_flasher_start() is invoked from the Bench tab when a target is plugged
     * in; starting the USB host unconditionally at boot is optional. */

    esp_err_t e = hc_mode_manager_start(on_mode, NULL);
    if (e != ESP_OK) {
        ESP_LOGW(TAG, "C6 link not up (%s)", esp_err_to_name(e));
        /* First-boot path: if the C6 didn't answer but a C6 image is on the SD
         * card, flash it (this is how the C6 gets its firmware without a USB
         * pass-through). After flashing, re-try the link. */
        if (hc_c6updater_should_offer(false, NULL)) {
            ESP_LOGI(TAG, "C6 image found on SD — flashing C6");
            if (hc_c6updater_run(NULL, NULL, NULL) == ESP_OK) {
                e = hc_mode_manager_start(on_mode, NULL);
            }
        }
        if (e != ESP_OK)
            ESP_LOGW(TAG, "continuing without C6 — flasher/UI still usable");
    }

    ESP_LOGI(TAG, "ready");
}
