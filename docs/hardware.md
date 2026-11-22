# Hardware notes — M5Stack Tab5

> Pin numbers are from the M5Stack Tab5 docs and the NuttX ESP32-P4-Tab5 port.
> Items marked **VERIFY** should be confirmed against the schematic for your
> board batch before you trust them with a reset line or a flash write.

## Chips

- **ESP32-P4** (SKU C145/K145): dual RISC-V @ 360 MHz, 16 MB flash, 32 MB octal
  PSRAM. Application processor — display, touch, SD, USB-OTG, audio.
- **ESP32-C6-MINI-1U**: Wi-Fi 6 (2.4 GHz only), BLE 5, 802.15.4. All radio.
  Internal 3D antenna + external MMCX option.

## P4 ↔ C6 link (SDIO2)

| Signal | P4 GPIO |
|---|---|
| SDIO2 D0 | 8 |
| SDIO2 D1 | 9 |
| SDIO2 D2 | 10 |
| SDIO2 D3 | 11 |
| SDIO2 CMD | 12 |
| SDIO2 CLK | 13 |
| C6 IO2 | 14 |
| **C6 RESET/EN** | **15  (VERIFY)** |

`hc_board.h` encodes these as `HC_PIN_C6_*`. `esp_hosted` 3.0.4+ already ships
Tab5 board defaults for the SDIO pins, so in WIFI mode you normally let it own
them; Wasp Nest only drives **C6 RESET** directly (for the reboot-based mode
switch and recovery).

### The bring-up link UART

Until the link moves onto SDIO (phase 2), `hc_link_host` / `hc_link_slave` use a
plain UART. Pick two free pins exposed on the **M5-Bus** header and cross them:

| | P4 side | C6 side |
|---|---|---|
| TX | `HC_LINK_UART_TX` (P4 GPIO 6, debug-UART TX) | ← |
| RX | `HC_LINK_UART_RX` (P4 GPIO 7, debug-UART RX) | → |

Set the matching pins in `hc_link_slave.c` on the C6. Any spare GPIOs work; the
defaults just reuse the documented M5-Bus debug UART (G6/G7). 921600 baud.

## microSD

SDMMC on the P4 (board docs report **G39–G44**). `hc_storage.c` mounts it at
`/sdcard` and writes captures to `/sdcard/waspnest/capNNN.pcap`. Start in 1-bit
mode (`slot.width = 1`) for reliability, move to 4-bit for throughput once solid.

## USB-A host port (the flasher)

The Tab5's full-size **USB-A** port is wired to the P4's **USB-OTG** controller,
which can act as host. (Note: the P4 *also* has a fixed-function USB-Serial-JTAG
used for its own console/flashing — that one is device-only. The flasher uses the
OTG host port, not that.)

A plugged-in target enumerates as one of:

- **native USB-Serial-JTAG** (ESP32-C3/C6/S3/H2/P4) → `cdc_acm_host`
- **CP210x / CH34x / FTDI USB-UART bridge** (classic ESP32 devkits) →
  the matching `usb_host_*_vcp` driver

`usb_host_vcp` auto-selects. Download-mode entry differs by type:

- **bridge:** classic auto-reset — RTS→EN, DTR→IO0.
- **native USB-Serial-JTAG:** the USB-JTAG reset combo.

`hc_loader_enter_download_mode(bool usb_serial_jtag)` implements both; the
flasher picks based on the enumerated device.

## Flashing the two chips on the Tab5

- **P4:** its USB-Serial-JTAG is on the Tab5's **USB-C** port — normal
  `idf.py flash`.
- **C6:** not directly exposed to USB-C. Options:
  1. Use `esptool` **through the P4** (the P4 can bridge to the C6 — the same
     mechanism `esp_hosted` uses to update slave firmware), or
  2. Reach the C6 UART/EN/IO0 via the M5-Bus header for a one-time flash.
  Document whichever route you settle on; (1) is the long-term clean path.

## Power

INA226 bus monitor at I2C `0x41` — read it for a battery/runtime readout on the
Status tab (`hc_board_power_read`, TODO).
