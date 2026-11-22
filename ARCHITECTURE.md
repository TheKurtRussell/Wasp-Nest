# Wasp Nest — Architecture

## The hardware reality that drives everything

The Tab5 is a **two-chip** board:

- **ESP32-P4** — dual RISC-V @ 360 MHz, 16 MB flash, 32 MB PSRAM. The
  application processor. It drives the 1280×720 MIPI-DSI display, the GT911
  touch, the microSD, the USB-OTG port, and audio. **It has no wireless radio.**
- **ESP32-C6** — Wi-Fi 6 (2.4 GHz), BLE, and **802.15.4** (Thread/Zigbee/Matter).
  All wireless lives here. It connects to the P4 over **SDIO2** (P4 GPIO 8–15,
  with the C6 RESET line also on a P4 GPIO).

The C6 can serve exactly one radio personality at a time:

- As an **`esp_hosted` Wi-Fi slave**, giving the P4 normal Wi-Fi via
  `esp_wifi_remote`, **or**
- As an **802.15.4 sniffer** running `esp_ieee802154` in promiscuous mode.

You cannot run stock `esp_hosted` Wi-Fi and 802.15.4 capture simultaneously.
Wasp Nest therefore treats the C6 as a **mode-switched radio service** and keeps
the two features cleanly decoupled on the P4.

## Component map

```
                      ┌─────────────────────────── ESP32-P4 (host) ───────────────────────────┐
                      │                                                                        │
   USB-A port ──────► │  hc_flasher ──► hc_esp_loader (esptool protocol)                       │
   (target board)     │     │                                                                  │
                      │     ▼                                                                   │
                      │  hc_ui (LVGL: Bench / Explorer / Status)                                │
                      │     ▲            ▲                                                      │
                      │     │            │                                                      │
                      │  hc_explorer ◄── hc_mode_manager ──► hc_board (C6 reset, display, SD)   │
                      │     │                 ▲                                                 │
                      │     ▼                 │ hc_link_host (framed msgs, CRC16)               │
                      │  hc_storage           │                                                 │
                      │  (pcap → microSD)     │                                                 │
                      └───────────────────────┼─────────────────────────────────────────────┘
                                              │  UART (bring-up) / SDIO2 custom RPC (phase 2)
                      ┌───────────────────────┼──────────────── ESP32-C6 (radio service) ─────┐
                      │                 hc_link_slave                                          │
                      │                        │                                               │
                      │                 hc_mode_agent ──► hc_radio_sniffer (esp_ieee802154)    │
                      │                        └────────► hc_radio_wifi   (esp_hosted slave)   │
                      └────────────────────────────────────────────────────────────────────┘
```

## Data flow: a captured frame

1. A Zigbee/Thread device transmits. The C6 radio, in promiscuous mode, raises
   `esp_ieee802154_receive_done()` (`hc_radio_sniffer.c`).
2. The ISR copies the MPDU + metadata (channel, RSSI, LQI, timestamp) into a
   FreeRTOS queue and **immediately releases the driver buffer**
   (`esp_ieee802154_receive_handle_done()` — required, or capture stalls).
3. `sniffer_task` pops it and calls back into `hc_mode_agent`, which packs an
   `HC_EV_FRAME` message (`hc_frame_hdr_t` + raw MPDU) and hands it to
   `hc_link_slave` → over the link to the P4.
4. `hc_link_host` validates the CRC and hands it to `hc_mode_manager`, which
   routes `HC_EV_FRAME` to `hc_explorer`.
5. `hc_explorer` dissects the MAC header (`hc_dissect`), forwards a compact row
   to `hc_ui`, and — if capturing — writes the raw frame to the pcap file on SD
   via `hc_storage`.

## Mode switching (the key design decision)

You chose **mode-switch**: the C6 moves between a Wi-Fi role and an 802.15.4
role on demand.

- **IDLE ⇄ SNIFFER** is a *soft* switch: `hc_mode_agent` tears down / brings up
  `esp_ieee802154` in place. No reboot.
- **→ WIFI** is a *reboot* switch. `esp_hosted`'s slave owns Wi-Fi init and its
  own SDIO slave task, so the clean, robust path is to reboot the C6 directly
  into the hosted-slave role. The agent persists the requested mode in
  `RTC_NOINIT` memory and calls `esp_restart()`; on the next boot it comes up
  straight into `hc_radio_wifi_run_hosted_slave()`. The P4 can also force this
  by pulsing the C6 RESET line (`hc_board_c6_reset()`).

Full sequence diagram in `docs/protocol.md`.

## Why a UART link first, SDIO later

The inter-chip link is abstracted behind `hc_link_host` / `hc_link_slave`, with
two backends:

- **UART (default):** trivial to bring up and scope, good to ~1 Mbaud — plenty
  for 802.15.4 (250 kbps on air). We start here so the sniffer is testable
  before any SDIO/esp_hosted complexity.
- **SDIO2 custom RPC (phase 2):** rides the same bus `esp_hosted` uses, so no
  extra wires, and is required once WIFI mode shares the bus. Espressif's own
  P4+C6 promiscuous-capture demos do exactly this via an `esp_hosted` CustomRpc
  channel — the pattern we follow.

## Design rules

- **The UI only calls the control modules** (`hc_mode_manager`, `hc_explorer`,
  `hc_flasher`). It never touches the link or radio directly.
- **The two features never share the C6 at once.** The Bench (USB flasher) lives
  entirely on the P4 and does not use the radio, so it works in any radio mode.
- **Shared wire structs live in `common/`** and are compiled into both images;
  `HC_PROTO_VERSION` is checked at the HELLO handshake so mismatched images fail
  loudly instead of corrupting silently.
