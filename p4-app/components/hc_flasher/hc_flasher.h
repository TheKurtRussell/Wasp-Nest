/*
 * Wasp Nest — P4 USB-host ESP flasher / serial bench
 *
 * Turns the Tab5's USB-A port into an esptool. A target ESP board plugs in by
 * its own USB cable; the P4 (USB host) enumerates it, drives the download-mode
 * reset sequence, and speaks the ESP serial bootloader protocol to erase/flash.
 * The same serial channel doubles as an on-screen serial monitor.
 *
 * Serial transport: the esp `usb_host_vcp` driver family auto-selects the right
 * backend for the plugged-in device:
 *     - native USB-Serial-JTAG (C3/C6/S3/P4/H2)   -> cdc_acm_host
 *     - CP210x bridge                              -> usb_host_cp210x_vcp
 *     - CH34x bridge                               -> usb_host_ch34x_vcp
 *     - FTDI bridge                                -> usb_host_ftdi_vcp
 * All register with usb_host_vcp, so we open via one call and get a uniform
 * read/write + line-control (DTR/RTS) handle.
 *
 * Bootloader protocol: classic SLIP-framed ROM loader (no stub) for v1 — works
 * on every ESP target and is simple to get right. Stub loader + compressed
 * writes are a speed optimization tracked for phase 2.
 */
#ifndef HC_FLASHER_H
#define HC_FLASHER_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    HC_TGT_UNKNOWN = 0,
    HC_TGT_ESP32, HC_TGT_ESP32S2, HC_TGT_ESP32S3,
    HC_TGT_ESP32C3, HC_TGT_ESP32C6, HC_TGT_ESP32C5,
    HC_TGT_ESP32H2, HC_TGT_ESP32P4,
} hc_target_chip_t;

typedef enum {
    HC_FL_IDLE, HC_FL_DEVICE_READY, HC_FL_SYNCED,
    HC_FL_ERASING, HC_FL_WRITING, HC_FL_DONE, HC_FL_ERROR,
} hc_flash_state_t;

/* Progress for the UI. */
typedef struct {
    hc_flash_state_t state;
    hc_target_chip_t chip;
    uint32_t bytes_total;
    uint32_t bytes_done;
    char     message[64];
} hc_flash_progress_t;

typedef void (*hc_flash_progress_cb_t)(const hc_flash_progress_t *p, void *ctx);

/* A region to write: data at a flash offset. For a typical merged-bin flash you
 * pass one entry at 0x0; for multi-file you pass several. Data is read from an
 * open FILE* path on the SD card (flash images live on the microSD). */
typedef struct {
    uint32_t offset;       /* flash offset, e.g. 0x0, 0x10000 */
    char     sd_path[96];  /* /sdcard/... image file */
} hc_flash_region_t;

esp_err_t hc_flasher_init(hc_flash_progress_cb_t cb, void *ctx);

/* Start the USB host + VCP stack and watch for a plugged-in target. The
 * progress callback fires HC_FL_DEVICE_READY when a board is enumerated. */
esp_err_t hc_flasher_start(void);

/* Detect the chip (SYNC + read chip magic/efuse). Fills chip_out. */
esp_err_t hc_flasher_detect(hc_target_chip_t *chip_out);

/* Flash one or more regions. Blocks; reports progress via the callback. */
esp_err_t hc_flasher_write(const hc_flash_region_t *regions, size_t n);

/* Serial monitor: pump bytes from the target to a line callback (post-flash,
 * or standalone). */
typedef void (*hc_serial_line_cb_t)(const char *line, void *ctx);
esp_err_t hc_flasher_monitor_start(uint32_t baud, hc_serial_line_cb_t cb, void *ctx);
esp_err_t hc_flasher_monitor_stop(void);

const char *hc_target_chip_name(hc_target_chip_t c);

#ifdef __cplusplus
}
#endif
#endif /* HC_FLASHER_H */
