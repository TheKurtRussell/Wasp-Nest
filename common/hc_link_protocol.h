/*
 * Wasp Nest — shared P4<->C6 link protocol
 *
 * This header is compiled into BOTH firmware images (the P4 host app and the
 * C6 radio-service slave). It defines the framed message protocol that travels
 * over the inter-chip transport (SDIO2 custom channel, with a UART fallback).
 *
 * Design rules:
 *   - Everything here is transport-agnostic. The link layer (hc_link_host on
 *     the P4, hc_link_slave on the C6) is responsible for framing/CRC; this
 *     header only describes message *payloads*.
 *   - Keep it C99, no dynamic allocation, fixed-size structs, little-endian.
 *     Both chips are little-endian RISC-V so no byte-swapping is needed.
 *   - Never reorder or renumber existing opcodes. Append only. The two images
 *     are flashed independently and must stay wire-compatible; HC_PROTO_VERSION
 *     is checked during the HELLO handshake.
 */
#ifndef HC_LINK_PROTOCOL_H
#define HC_LINK_PROTOCOL_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#define HC_PROTO_VERSION      2   /* 2 = adds Wi-Fi recon mode (Wasp Nest) */
#define HC_LINK_MAGIC         0xA5C6u   /* every frame header starts with this */
#define HC_MAX_PAYLOAD        1280      /* bytes; fits a full 802.15.4 capture + meta */

/* ---- Message opcodes ----------------------------------------------------- */
/* Host (P4) -> Slave (C6) commands are 0x01..0x3F.
 * Slave (C6) -> Host (P4) events are    0x80..0xBF.
 * This split lets the link layer sanity-check direction. */
typedef enum {
    /* --- control / lifecycle --- */
    HC_OP_HELLO          = 0x01,  /* host->slave: hc_hello_t, slave replies HC_EV_HELLO */
    HC_OP_SET_MODE       = 0x02,  /* host->slave: hc_set_mode_t */
    HC_OP_PING           = 0x03,  /* host->slave: empty; slave replies HC_EV_PONG */

    /* --- 802.15.4 sniffer control --- */
    HC_OP_SNIFF_START    = 0x10,  /* host->slave: hc_sniff_cfg_t */
    HC_OP_SNIFF_STOP     = 0x11,  /* host->slave: empty */
    HC_OP_SNIFF_SET_CHAN = 0x12,  /* host->slave: hc_set_chan_t (hop without restart) */
    HC_OP_SCAN_ENERGY    = 0x13,  /* host->slave: hc_energy_scan_t */
    HC_OP_SCAN_ACTIVE    = 0x14,  /* host->slave: hc_active_scan_t (beacon request sweep) */

    /* --- wifi-mode (esp_hosted) control --- */
    HC_OP_WIFI_ENTER     = 0x20,  /* host->slave: empty; hand radio to esp_hosted slave */

    /* --- wifi-sniffer (recon) control --- */
    HC_OP_WSCAN_START    = 0x30,  /* host->slave: hc_wscan_cfg_t */
    HC_OP_WSCAN_STOP     = 0x31,  /* host->slave: empty */
    HC_OP_WSCAN_SET_CHAN = 0x32,  /* host->slave: hc_set_chan_t (1..14) */

    /* --- ble-scanner (recon) control --- */
    HC_OP_BLESCAN_START  = 0x40,  /* host->slave: hc_blescan_cfg_t */
    HC_OP_BLESCAN_STOP   = 0x41,  /* host->slave: empty */

    /* --- slave->host events --- */
    HC_EV_HELLO          = 0x80,  /* hc_hello_t (slave's view) */
    HC_EV_PONG           = 0x81,  /* empty */
    HC_EV_MODE           = 0x82,  /* hc_mode_state_t: ack/report of current mode */
    HC_EV_LOG            = 0x83,  /* hc_log_t: ascii log line from the C6 */
    HC_EV_FRAME          = 0x90,  /* hc_frame_hdr_t + raw 802.15.4 bytes */
    HC_EV_ENERGY         = 0x91,  /* hc_energy_result_t */
    HC_EV_SCAN_RESULT    = 0x92,  /* hc_scan_result_t (one discovered network) */
    HC_EV_SCAN_DONE      = 0x93,  /* empty */

    /* --- wifi-sniffer events --- */
    HC_EV_WIFI_AP        = 0xA0,  /* hc_wifi_ap_evt_t (one AP/probe sighting) */
    HC_EV_WIFI_FRAME     = 0xA1,  /* hc_frame_hdr_t + raw 802.11 (pcap, optional) */
    HC_EV_WSCAN_DONE     = 0xA2,  /* empty (a one-shot scan sweep finished) */

    /* --- ble-scanner events --- */
    HC_EV_BLE_DEV        = 0xB0,  /* hc_ble_dev_evt_t (one BLE advertiser) */
} hc_opcode_t;

/* ---- Radio modes --------------------------------------------------------- */
typedef enum {
    HC_MODE_IDLE    = 0,   /* radio parked, low power */
    HC_MODE_SNIFFER = 1,   /* esp_ieee802154 promiscuous capture */
    HC_MODE_WIFI    = 2,   /* esp_hosted slave: C6 is a Wi-Fi coprocessor for P4 */
    HC_MODE_WIFI_SNIFFER = 3, /* esp_wifi promiscuous recon (soft switch; NOT esp_hosted) */
    HC_MODE_BLE_SCANNER  = 4, /* NimBLE observer: BLE advertisement scan */
} hc_radio_mode_t;

/* ---- Frame header (HC_LINK_MAGIC + this) --------------------------------- */
/* The link layer prepends this to every payload and appends a CRC16. */
typedef struct __attribute__((packed)) {
    uint16_t magic;     /* HC_LINK_MAGIC */
    uint8_t  opcode;    /* hc_opcode_t */
    uint8_t  flags;     /* reserved, 0 */
    uint16_t seq;       /* rolling sequence, per-direction */
    uint16_t len;       /* payload length, <= HC_MAX_PAYLOAD */
    /* ... payload[len] ... uint16_t crc16 (CRC-16/CCITT-FALSE over hdr+payload) */
} hc_link_hdr_t;

/* ---- Payload structs ----------------------------------------------------- */

typedef struct __attribute__((packed)) {
    uint8_t  proto_version;  /* HC_PROTO_VERSION */
    uint8_t  chip;           /* 0 = P4, 1 = C6 */
    uint32_t fw_build;       /* unix build time, for quick mismatch spotting */
    char     fw_semver[16];  /* e.g. "0.1.0" */
} hc_hello_t;

typedef struct __attribute__((packed)) {
    uint8_t mode;            /* hc_radio_mode_t */
} hc_set_mode_t;

typedef struct __attribute__((packed)) {
    uint8_t  mode;           /* hc_radio_mode_t now active */
    uint8_t  ok;             /* 1 = switch succeeded, 0 = failed */
    uint8_t  reason;         /* hc_mode_fail_t on failure */
} hc_mode_state_t;

typedef enum {
    HC_MODEFAIL_NONE      = 0,
    HC_MODEFAIL_BUSY      = 1,  /* a capture/scan is still running */
    HC_MODEFAIL_UNSUPPORTED = 2,
    HC_MODEFAIL_RADIO_ERR = 3,
} hc_mode_fail_t;

typedef struct __attribute__((packed)) {
    uint8_t  channel;        /* 11..26; 0 = use hop list below */
    uint8_t  hop_count;      /* 0 = fixed channel; else number of channels in hop[] */
    uint16_t hop_dwell_ms;   /* dwell per channel when hopping */
    uint8_t  promiscuous;    /* 1 = accept all frames incl. bad FCS */
    uint8_t  hop[16];        /* channel hop list (valid entries = hop_count) */
} hc_sniff_cfg_t;

typedef struct __attribute__((packed)) {
    uint8_t channel;         /* 11..26 */
} hc_set_chan_t;

typedef struct __attribute__((packed)) {
    uint32_t channel_mask;   /* bit n set => scan channel (11 + n) */
    uint16_t dwell_ms;       /* energy dwell per channel */
} hc_energy_scan_t;

typedef struct __attribute__((packed)) {
    uint32_t channel_mask;   /* channels to sweep with beacon requests */
    uint16_t dwell_ms;
} hc_active_scan_t;

/* Per captured frame, sent with HC_EV_FRAME, followed immediately by `len`
 * raw PHY payload bytes (the MPDU, optionally including FCS). */
typedef struct __attribute__((packed)) {
    uint64_t ts_us;          /* C6 monotonic timestamp, microseconds */
    uint8_t  channel;        /* channel the frame was heard on */
    int8_t   rssi;           /* dBm */
    uint8_t  lqi;            /* link quality 0..255 */
    uint8_t  fcs_ok;         /* 1 = CRC valid, 0 = bad/unknown (promiscuous) */
    uint16_t len;            /* raw MPDU length that follows */
} hc_frame_hdr_t;

typedef struct __attribute__((packed)) {
    uint8_t channel;         /* 11..26 */
    int8_t  peak_rssi;       /* strongest energy seen during dwell */
    int8_t  avg_rssi;        /* average */
} hc_energy_result_t;

/* One discovered PAN from an active scan (parsed from a beacon). */
typedef struct __attribute__((packed)) {
    uint16_t pan_id;
    uint8_t  channel;
    int8_t   rssi;
    uint8_t  lqi;
    uint8_t  permit_join;    /* association permit bit from superframe spec */
    uint8_t  stack_guess;    /* hc_stack_guess_t: Zigbee / Thread / unknown */
    uint8_t  ext_pan_id[8];  /* 0s if not present */
} hc_scan_result_t;

typedef enum {
    HC_STACK_UNKNOWN = 0,
    HC_STACK_ZIGBEE  = 1,
    HC_STACK_THREAD  = 2,    /* 802.15.4 w/ Thread-style payload heuristics */
    HC_STACK_MATTER  = 3,    /* Matter-over-Thread, best-effort heuristic */
} hc_stack_guess_t;

typedef struct __attribute__((packed)) {
    uint8_t  level;          /* 0=err 1=warn 2=info 3=debug */
    char     msg[96];
} hc_log_t;

/* ---- Wi-Fi recon (Wasp Nest) --------------------------------------------- */
/* Band select is future-proofing: the C6 serves 2.4 GHz only, so bit 1 (5 GHz)
 * is for the C5 foragers that will speak this same protocol over ESP-NOW. */
#define HC_WBAND_2G4   (1u << 0)
#define HC_WBAND_5G    (1u << 1)   /* C5 foragers only; C6 reports UNSUPPORTED */

typedef struct __attribute__((packed)) {
    uint8_t  band_mask;      /* HC_WBAND_* ; C6 accepts 2G4 only */
    uint8_t  hop_count;      /* 0 = fixed on hop[0]; else number of channels */
    uint16_t hop_dwell_ms;   /* dwell per channel when hopping (e.g. 250) */
    uint8_t  promiscuous;    /* 1 = keep all mgmt frames incl. bad FCS */
    uint8_t  want_raw;       /* 1 = also emit HC_EV_WIFI_FRAME for pcap */
    uint8_t  hop[14];        /* 2.4 GHz channel list, valid entries = hop_count */
} hc_wscan_cfg_t;

/* AP/probe flags */
#define HC_WIFI_F_PROBE   (1u << 0)  /* seen via probe req/resp, not beacon */
#define HC_WIFI_F_HIDDEN  (1u << 1)  /* beacon with empty/zeroed SSID */
#define HC_WIFI_F_WPA3    (1u << 2)  /* RSN with SAE AKM (best-effort) */

/* One AP (or probe) sighting, sent with HC_EV_WIFI_AP. Compact so the P4 can
 * dedupe into its live list; raw frames (if wanted) ride HC_EV_WIFI_FRAME. */
typedef struct __attribute__((packed)) {
    uint8_t  bssid[6];
    int8_t   rssi;           /* dBm */
    uint8_t  channel;        /* 1..14 */
    uint8_t  flags;          /* HC_WIFI_F_* */
    uint8_t  ssid_len;       /* 0..32 */
    char     ssid[32];       /* not NUL-terminated; use ssid_len */
} hc_wifi_ap_evt_t;

/* ---- BLE recon (Wasp Nest) ----------------------------------------------- */
typedef struct __attribute__((packed)) {
    uint8_t  active;         /* 1 = active scan (sends scan reqs); 0 = passive */
    uint16_t itvl;           /* scan interval in 0.625ms units; 0 = driver default */
    uint16_t window;         /* scan window  in 0.625ms units; 0 = driver default */
} hc_blescan_cfg_t;

/* BLE device flags */
#define HC_BLE_F_CONNECTABLE (1u << 0)  /* connectable advertisement */
#define HC_BLE_F_NAMED       (1u << 1)  /* advertised a (complete/short) name */
#define HC_BLE_F_APPLE       (1u << 2)  /* Apple mfg data (FindMy/AirTag heuristic) */
#define HC_BLE_F_RANDOM      (1u << 3)  /* random address type */

/* One BLE advertiser sighting, sent with HC_EV_BLE_DEV. */
typedef struct __attribute__((packed)) {
    uint8_t  addr[6];        /* advertiser address (little-endian as reported) */
    uint8_t  addr_type;      /* NimBLE BLE_ADDR_* */
    int8_t   rssi;           /* dBm */
    uint8_t  flags;          /* HC_BLE_F_* */
    uint8_t  name_len;       /* 0..29 */
    char     name[29];       /* not NUL-terminated; use name_len */
} hc_ble_dev_evt_t;

#ifdef __cplusplus
}
#endif
#endif /* HC_LINK_PROTOCOL_H */
