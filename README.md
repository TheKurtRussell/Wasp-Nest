# Wasp Nest

A wireless **recon and auditing** workbench for the **M5Stack Tab5**
(ESP32-P4 + ESP32-C6), built on the Honeycomb architecture and extended toward a
Marauder/Bruce-class multi-tool with a proper large touchscreen.

> ⚠️ **Authorized testing only.** Wasp Nest is for assessing networks and devices
> you own or have explicit written permission to test. Active features
> (deauth, rogue AP, portals — *not yet in this tree*) are illegal to use against
> networks you don't control in most jurisdictions. Use them only on your own lab
> gear. The author assumes no liability for misuse.

## Relationship to Honeycomb

Wasp Nest is a **fork of [Honeycomb](https://github.com/TheKurtRussell/honeycomb)**.
Honeycomb stays a focused flash/serial bench + 802.15.4 explorer; Wasp Nest is the
branch that grows Wi-Fi/BLE recon (and, later, authorized active modules) on top.

**Shared core:** `common/` (the P4↔C6 wire protocol), the link layer, the UI
shell and the pcap sink are the same in both repos. Treat Honeycomb's versions as
the source of truth and resync changes by hand until/unless this is promoted to a
git submodule. Keep the shared pieces in step so the two don't silently drift.

To avoid collisions when both firmwares touch the same Tab5 / SD card, Wasp Nest
uses its **own SD paths** (`/sdcard/waspnest/...`) and its **own Pages build**, so
it never overwrites Honeycomb's C6 image or captures.

## What it is

The Tab5 is two chips: the **P4** application processor (no radio) and a **C6**
co-processor that owns all wireless, over SDIO2. The C6 serves one radio
personality at a time, so Wasp Nest treats it as a **mode-switched radio service**
and adds Wi-Fi recon as a new *soft-switched* mode beside the 802.15.4 sniffer —
no esp_hosted, no reboot.

| Mode | C6 role |
|---|---|
| `HC_MODE_SNIFFER` | 802.15.4 (Thread/Zigbee/Matter) promiscuous capture |
| `HC_MODE_WIFI_SNIFFER` | **NEW** — esp_wifi promiscuous Wi-Fi recon (2.4 GHz) |
| `HC_MODE_WIFI` | esp_hosted slave (P4 gets normal Wi-Fi; reboot switch) |

5 GHz is out of the C6's reach (it's 2.4-only), so dual-band coverage comes from
**C5 forager nodes** that speak this same wire protocol over ESP-NOW — see the
architecture whiteboard / `docs/roadmap.md`.

## Status

Built on Honeycomb's scaffold (clean interfaces, on-hardware bring-up TODOs).

| Piece | Status |
|---|---|
| Wi-Fi recon protocol (opcodes, mode, structs) | **In** — `common/hc_link_protocol.h`, proto v2 |
| C6 Wi-Fi sniffer (`hc_radio_wifi_sniffer`) | **In** — esp_wifi promiscuous, beacon/probe parse |
| C6 mode agent wiring | **In** — `HC_MODE_WIFI_SNIFFER` + `HC_OP_WSCAN_*` |
| P4 mode manager wiring | **In** — scan controls + `HC_EV_WIFI_AP` logging |
| Channel hopping (C6) | **TODO** — one spot in `hc_wifi_sniffer_start` |
| P4 `hc_wifi_recon` dedupe + Wi-Fi UI tab | **TODO** — AP events currently just logged |
| BLE recon | **Next** — same shape, swap the C6 radio |
| C5 foragers (5 GHz over ESP-NOW) | **Next** — reuse these wire structs |
| Active modules (authorized only) | **Later** |

## First test (no display, no USB host)

1. Build & flash both chips (see `docs/flashing.md`) or use the web flasher.
2. Switch the C6 to `HC_MODE_WIFI_SNIFFER`.
3. Send `HC_OP_WSCAN_START` with
   `{ band_mask=HC_WBAND_2G4, hop_count=1, hop[0]=6, promiscuous=1, want_raw=0 }`.
4. Watch the P4 serial console: every `HC_EV_WIFI_AP` logs as
   `AP <bssid> ch=6 rssi=.. ssid=".."`. APs on channel 6 streaming in means the
   whole C6 → link → P4 pipeline works. Then add channel hopping and the UI tab.

## Layout

Same as Honeycomb, plus `c6-slave/components/hc_radio_wifi_sniffer/`. See
`ARCHITECTURE.md` for the design and `docs/` for protocol, hardware and flashing.
