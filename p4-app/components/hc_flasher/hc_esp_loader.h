/*
 * Wasp Nest — ESP serial bootloader protocol (host side)
 *
 * Minimal, self-contained implementation of the SLIP-framed ROM loader protocol
 * that esptool speaks. Transport is abstracted: you provide read/write/line-ctrl
 * function pointers (wired to the USB VCP handle in hc_flasher.c), so this file
 * has no USB dependency and can be unit-tested over a loopback.
 *
 * Protocol reference: espressif/esptool "Serial protocol" docs and the
 * esp-serial-flasher project. Commands implemented: SYNC, READ_REG,
 * SPI_ATTACH, FLASH_BEGIN, FLASH_DATA, FLASH_END, CHANGE_BAUDRATE.
 */
#ifndef HC_ESP_LOADER_H
#define HC_ESP_LOADER_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Transport the loader drives (supplied by hc_flasher). */
typedef struct {
    int  (*write)(const uint8_t *buf, size_t len, void *io);   /* ret bytes or <0 */
    int  (*read)(uint8_t *buf, size_t len, uint32_t timeout_ms, void *io);
    void (*set_dtr_rts)(bool dtr, bool rts, void *io);         /* auto-reset lines */
    void (*set_baud)(uint32_t baud, void *io);                 /* host-side baud */
    void *io;
} hc_loader_transport_t;

/* ESP bootloader command opcodes. */
typedef enum {
    LDR_SYNC         = 0x08,
    LDR_READ_REG     = 0x0A,
    LDR_SPI_ATTACH   = 0x0D,
    LDR_CHANGE_BAUD  = 0x0F,
    LDR_FLASH_BEGIN  = 0x02,
    LDR_FLASH_DATA   = 0x03,
    LDR_FLASH_END    = 0x04,
} hc_loader_cmd_t;

void      hc_loader_bind(const hc_loader_transport_t *t);

/* Drive the classic auto-reset: RTS->EN, DTR->IO0 so the target boots into the
 * serial download ROM. (Native USB-serial-JTAG targets use a different combo;
 * hc_flasher picks the right one by device type.) */
void      hc_loader_enter_download_mode(bool usb_serial_jtag);

/* Low-level: send one command, wait for the matching response. */
esp_err_t hc_loader_command(hc_loader_cmd_t cmd, const uint8_t *data, uint16_t len,
                            uint32_t chk, uint32_t *val_out, uint32_t timeout_ms);

/* High-level flows. */
esp_err_t hc_loader_sync(void);                          /* retried SYNC */
esp_err_t hc_loader_read_reg(uint32_t addr, uint32_t *out);
esp_err_t hc_loader_flash_begin(uint32_t size, uint32_t offset, uint32_t block);
esp_err_t hc_loader_flash_block(const uint8_t *data, uint16_t len, uint32_t seq);
esp_err_t hc_loader_flash_end(bool reboot);

/* SLIP + checksum helpers (exposed for tests). */
uint8_t   hc_loader_checksum(const uint8_t *data, uint16_t len); /* XOR seed 0xEF */

#ifdef __cplusplus
}
#endif
#endif /* HC_ESP_LOADER_H */
