/*
 * Wasp Nest — P4 UI (LVGL)
 *
 * A 3-tab touchscreen shell:
 *   BENCH    — USB flasher + serial monitor (hc_flasher)
 *   EXPLORER — 802.15.4 sniffer, channel picker, energy heatmap, scan list
 *   STATUS   — C6 link state, mode badge, battery, capture file
 *
 * The UI only calls hc_mode_manager / hc_explorer / hc_flasher. It never touches
 * the link or the radio directly. All update_* functions are safe to call from
 * other tasks (they marshal onto the LVGL task via lv_async_call).
 */
#ifndef HC_UI_H
#define HC_UI_H

#include "esp_err.h"
#include "hc_explorer.h"
#include "hc_flasher.h"
#include "hc_link_protocol.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Pass the lv_display_t* from the board BSP (as void* to avoid leaking the LVGL
 * type into non-UI code). If NULL, the UI runs in a no-op headless mode. */
esp_err_t hc_ui_init(void *lv_display);

/* thread-safe update hooks */
void hc_ui_add_frame(const hc_dissected_t *row);
void hc_ui_set_energy(const hc_energy_result_t *e);
void hc_ui_add_scan(const hc_scan_result_t *s);
void hc_ui_set_mode(hc_radio_mode_t mode, bool ok);
void hc_ui_set_flash_progress(const hc_flash_progress_t *p);
void hc_ui_log(const char *line);

#ifdef __cplusplus
}
#endif
#endif /* HC_UI_H */
