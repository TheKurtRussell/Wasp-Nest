/*
 * Wasp Nest — P4 Explorer: 802.15.4 frame dissection + capture sink
 *
 * Receives HC_EV_FRAME / HC_EV_ENERGY / HC_EV_SCAN_RESULT from the C6 (via the
 * mode manager), dissects each MAC frame into a compact human-readable record
 * for the UI, and (when capturing) writes raw frames to a pcap file on SD for
 * Wireshark.
 */
#ifndef HC_EXPLORER_H
#define HC_EXPLORER_H

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"
#include "hc_link_protocol.h"
#include "hc_ieee802154_defs.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Dissected view of one frame, ready for a UI row. */
typedef struct {
    uint64_t ts_us;
    uint8_t  channel;
    int8_t   rssi;
    uint8_t  lqi;
    bool     fcs_ok;
    hc_frame_type_t type;
    uint8_t  version;
    bool     ack_req;
    bool     security;
    uint16_t src_pan;
    uint16_t dst_pan;
    char     src[20];     /* "0x1234" or "aabb..." */
    char     dst[20];
    uint8_t  seq;
    const char *type_str; /* "DATA", "BEACON", "ACK", "CMD:BeaconReq", ... */
    hc_stack_guess_t stack;
    uint16_t raw_len;
} hc_dissected_t;

/* UI hook: called for each dissected frame (on the explorer task). */
typedef void (*hc_explorer_row_cb_t)(const hc_dissected_t *row, void *ctx);
typedef void (*hc_explorer_energy_cb_t)(const hc_energy_result_t *e, void *ctx);
typedef void (*hc_explorer_scan_cb_t)(const hc_scan_result_t *s, void *ctx);

typedef struct {
    hc_explorer_row_cb_t    on_row;
    hc_explorer_energy_cb_t on_energy;
    hc_explorer_scan_cb_t   on_scan;
    void                   *ctx;
} hc_explorer_callbacks_t;

esp_err_t hc_explorer_init(const hc_explorer_callbacks_t *cb);

/* Feed raw link events in (called by the mode manager's RX fan-out). */
void hc_explorer_on_frame(const hc_frame_hdr_t *meta, const uint8_t *mpdu);
void hc_explorer_on_energy(const hc_energy_result_t *e);
void hc_explorer_on_scan(const hc_scan_result_t *s);

/* pcap capture to SD. Returns the path used (for the UI to display). */
esp_err_t hc_explorer_capture_start(char *path_out, size_t path_cap);
esp_err_t hc_explorer_capture_stop(void);
bool      hc_explorer_is_capturing(void);
uint32_t  hc_explorer_frame_count(void);

/* Pure dissector (also usable in a host unit test). Returns false if the buffer
 * is too short to be a valid MHR. */
bool hc_dissect(const hc_frame_hdr_t *meta, const uint8_t *mpdu, hc_dissected_t *out);

#ifdef __cplusplus
}
#endif
#endif /* HC_EXPLORER_H */
