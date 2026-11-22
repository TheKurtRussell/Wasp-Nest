/*
 * Wasp Nest — M5Stack Tab5 board support (P4)
 *
 * Centralizes every Tab5 pin/peripheral the app touches, so hardware quirks
 * live in one file. Pin numbers come from the M5Stack Tab5 docs + NuttX port;
 * anything marked VERIFY should be checked against your board's schematic before
 * trusting it (M5 occasionally revises pinmaps between batches).
 */
#ifndef HC_BOARD_H
#define HC_BOARD_H

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ---- P4 <-> C6 SDIO2 bus ------------------------------------------------- */
/* Authoritative pinmap, cross-checked against the M5 Tab5 docs AND the ESPHome
 * esp32_hosted config for this board:
 *   CLK=G12  CMD=G13  D0=G11  D1=G10  D2=G9  D3=G8   RESET=G15  IO2=G14
 * (An earlier revision of this file had the data lines reversed — fixed.) */
#define HC_PIN_C6_SDIO_CLK   12
#define HC_PIN_C6_SDIO_CMD   13
#define HC_PIN_C6_SDIO_D0    11
#define HC_PIN_C6_SDIO_D1    10
#define HC_PIN_C6_SDIO_D2    9
#define HC_PIN_C6_SDIO_D3    8
#define HC_PIN_C6_RESET      15   /* C6 EN/RESET, P4-controlled */
#define HC_PIN_C6_IO2        14   /* C6 strap line; candidate BOOT pin for SDIO download (TEST) */

/* ---- microSD (SDMMC) — see hc_storage.c; pins reported as G39..G44 -------- */

/* Bring up display + touch + backlight via the M5Stack Tab5 BSP (handles the
 * MIPI-DSI panel, GT911/ST7123 touch, the display LDO rail, and LVGL). */
esp_err_t hc_board_init(void);

/* The LVGL display created by the BSP, as void* (so non-UI code needn't pull in
 * LVGL types). NULL if the display didn't come up. hc_ui_init() takes this. */
void *hc_board_lvgl_display(void);

/* Pulse the C6 reset line (used by the mode manager). */
void hc_board_c6_reset(void);

/* INA226 battery/bus gauge (Tab5 has one at 0x41). Returns millivolts/milliamps.
 * Handy to show runtime on the status bar. */
esp_err_t hc_board_power_read(int *mv, int *ma);

#ifdef __cplusplus
}
#endif
#endif /* HC_BOARD_H */
