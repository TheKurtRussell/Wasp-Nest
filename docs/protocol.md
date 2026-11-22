# Inter-chip protocol

Defined in `common/hc_link_protocol.h`. Compiled into both images.

## Framing

Every message on the link (either direction):

```
┌────────┬────────┬───────┬───────┬───────┬───────────────┬────────┐
│ magic  │ opcode │ flags │  seq  │  len  │   payload…    │ crc16  │
│ 0xA5C6 │  u8    │  u8   │  u16  │  u16  │   len bytes   │  u16   │
└────────┴────────┴───────┴───────┴───────┴───────────────┴────────┘
        └──────────────── hc_link_hdr_t ────────────────┘
```

- Little-endian throughout (both chips are LE RISC-V; no swapping).
- `magic` lets the receiver resync after any corruption.
- `crc16` is CRC-16/CCITT-FALSE over everything after `magic` (header tail +
  payload). Identical code in `hc_link_host.c` and `hc_link_slave.c` — keep them
  in lock-step.
- `len ≤ HC_MAX_PAYLOAD` (1280), which fits a full 127-byte 802.15.4 capture plus
  its `hc_frame_hdr_t` metadata with wide margin.

## Opcodes

Host→slave commands are `0x01–0x3F`; slave→host events are `0x80–0xBF`. The link
layer can sanity-check direction from the range.

| Opcode | Dir | Payload | Meaning |
|---|---|---|---|
| `HC_OP_HELLO` | P4→C6 | `hc_hello_t` | handshake; C6 replies `HC_EV_HELLO` |
| `HC_OP_SET_MODE` | P4→C6 | `hc_set_mode_t` | IDLE / SNIFFER / WIFI |
| `HC_OP_PING` | P4→C6 | — | liveness; replies `HC_EV_PONG` |
| `HC_OP_SNIFF_START` | P4→C6 | `hc_sniff_cfg_t` | begin capture (fixed/hop) |
| `HC_OP_SNIFF_STOP` | P4→C6 | — | stop capture |
| `HC_OP_SNIFF_SET_CHAN` | P4→C6 | `hc_set_chan_t` | hop without restart |
| `HC_OP_SCAN_ENERGY` | P4→C6 | `hc_energy_scan_t` | per-channel RSSI sweep |
| `HC_OP_SCAN_ACTIVE` | P4→C6 | `hc_active_scan_t` | beacon-request sweep |
| `HC_OP_WIFI_ENTER` | P4→C6 | — | hand radio to esp_hosted (reboots C6) |
| `HC_EV_HELLO` | C6→P4 | `hc_hello_t` | identity + proto version |
| `HC_EV_MODE` | C6→P4 | `hc_mode_state_t` | mode ack/report |
| `HC_EV_FRAME` | C6→P4 | `hc_frame_hdr_t`+MPDU | one captured frame |
| `HC_EV_ENERGY` | C6→P4 | `hc_energy_result_t` | one channel's energy |
| `HC_EV_SCAN_RESULT` | C6→P4 | `hc_scan_result_t` | one discovered PAN |
| `HC_EV_SCAN_DONE` | C6→P4 | — | scan finished |
| `HC_EV_LOG` | C6→P4 | `hc_log_t` | ascii log line (shows on Status tab) |

## Handshake

```
P4                         C6
│   HC_OP_HELLO (proto=1)  │
│ ───────────────────────► │
│   HC_EV_HELLO (proto=1)  │
│ ◄─────────────────────── │
│  check proto match       │
```

`hc_link_host_handshake()` fails with `ESP_ERR_INVALID_VERSION` on mismatch, so
you can't accidentally pair incompatible P4/C6 images.

## Mode switching sequence

### Soft switch (IDLE ⇄ SNIFFER)

```
P4: HC_OP_SET_MODE{SNIFFER} ─► C6: enter_sniffer() [esp_ieee802154 up]
C6: HC_EV_MODE{SNIFFER, ok} ─► P4: UI badge -> SNIFFER
P4: HC_OP_SNIFF_START{cfg}  ─► C6: capture begins
                               C6: HC_EV_FRAME … (stream)
```

### Reboot switch (→ WIFI)

```
P4: HC_OP_SET_MODE{WIFI} ─► C6: HC_EV_MODE{WIFI, ok}   (ack first)
                            C6: persist mode in RTC_NOINIT, esp_restart()
                            C6: cold boot ─► hc_radio_wifi_run_hosted_slave()
P4: (re)init esp_wifi_remote so the hosted link comes up on SDIO
```

If the C6 ever wedges, the P4 pulses `HC_PIN_C6_RESET` (`hc_board_c6_reset()`)
to force a clean reboot and re-handshakes.

## Backend note

The current backend is **UART** (`HC_LINK_UART_*` in both link files). Phase 2
moves framing onto an SDIO2 custom-RPC channel shared with `esp_hosted`; the
opcode/struct layer above does not change.
