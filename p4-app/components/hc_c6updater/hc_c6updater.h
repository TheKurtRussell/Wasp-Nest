/*
 * Wasp Nest — C6 firmware updater (P4 flashes the C6 from SD)
 *
 * The C6 has no USB of its own on the Tab5, so instead of a browser/USB
 * pass-through we let the P4 flash it directly using Espressif's
 * `esp-serial-flasher` library, which supports ESP32-P4 host -> ESP32-C6 target
 * over SDIO / UART. The image is read from the microSD card.
 *
 * Flow: hold C6 in download mode (RESET + BOOT pins) -> connect (with stub) ->
 * flash the merged image at offset 0 -> reset the C6 back into normal boot.
 *
 * This is what makes Wasp Nest installable without any special programmer:
 *   1. web-flash the P4 app over USB-C
 *   2. drop c6_service.bin on the SD card
 *   3. this runs once and the C6 is done
 */
#ifndef HC_C6UPDATER_H
#define HC_C6UPDATER_H

#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*hc_c6upd_progress_cb_t)(int pct, const char *msg, void *ctx);

/* Flash the C6 from a merged image on SD (default:
 * /sdcard/waspnest/c6_service.bin). Blocks until done. */
esp_err_t hc_c6updater_run(const char *sd_path, hc_c6upd_progress_cb_t cb, void *ctx);

/* Is a C6 image present on SD and the C6 apparently unprogrammed / mismatched?
 * Used at boot to decide whether to offer/auto-run the update. Cheap heuristic:
 * returns true if the SD image exists and the C6 did not answer HELLO. */
bool hc_c6updater_should_offer(bool c6_handshake_ok, const char *sd_path);

#ifdef __cplusplus
}
#endif
#endif /* HC_C6UPDATER_H */
