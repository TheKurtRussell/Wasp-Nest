/*
 * Wasp Nest — P4 Explorer implementation
 *
 * The dissector parses the 802.15.4 MAC header (MHR) per IEEE 802.15.4-2015
 * clause 7. It handles the common 2003/2006/2015 frame-version layouts enough
 * to label traffic and extract addressing; it does not decrypt secured payloads
 * (that's a phase-3 "participation" concern).
 */
#include "hc_explorer.h"
#include "hc_storage.h"

#include <stdio.h>
#include <string.h>
#include "esp_log.h"

#define TAG "hc_expl"

static hc_explorer_callbacks_t s_cb;
static bool     s_capturing;
static uint32_t s_count;

/* ---- helpers ------------------------------------------------------------- */
static void fmt_short(char *dst, size_t cap, uint16_t a)
{
    snprintf(dst, cap, "0x%04X", a);
}
static void fmt_ext(char *dst, size_t cap, const uint8_t *a /*8 bytes, LE*/)
{
    /* print big-endian for readability */
    snprintf(dst, cap, "%02X%02X%02X%02X%02X%02X%02X%02X",
             a[7], a[6], a[5], a[4], a[3], a[2], a[1], a[0]);
}

static const char *cmd_name(uint8_t id)
{
    switch (id) {
    case HC_CMD_ASSOC_REQ:       return "CMD:AssocReq";
    case HC_CMD_ASSOC_RESP:      return "CMD:AssocResp";
    case HC_CMD_DISASSOC_NOTIFY: return "CMD:Disassoc";
    case HC_CMD_DATA_REQ:        return "CMD:DataReq";
    case HC_CMD_PANID_CONFLICT:  return "CMD:PANConflict";
    case HC_CMD_ORPHAN_NOTIFY:   return "CMD:Orphan";
    case HC_CMD_BEACON_REQ:      return "CMD:BeaconReq";
    case HC_CMD_COORD_REALIGN:   return "CMD:Realign";
    case HC_CMD_GTS_REQ:         return "CMD:GTSReq";
    default:                     return "CMD:?";
    }
}

bool hc_dissect(const hc_frame_hdr_t *meta, const uint8_t *mpdu, hc_dissected_t *o)
{
    if (!meta || !mpdu || !o || meta->len < 3) return false;
    memset(o, 0, sizeof(*o));
    o->ts_us = meta->ts_us; o->channel = meta->channel; o->rssi = meta->rssi;
    o->lqi = meta->lqi; o->fcs_ok = meta->fcs_ok; o->raw_len = meta->len;

    uint16_t fcf = (uint16_t)mpdu[0] | ((uint16_t)mpdu[1] << 8);
    o->type     = hc_fcf_type(fcf);
    o->version  = hc_fcf_version(fcf);
    o->ack_req  = (fcf & HC_FCF_ACK_REQ) != 0;
    o->security = (fcf & HC_FCF_SEC_EN) != 0;
    bool pan_compr = (fcf & HC_FCF_PAN_ID_COMPR) != 0;
    hc_addr_mode_t dmode = hc_fcf_dst_mode(fcf);
    hc_addr_mode_t smode = hc_fcf_src_mode(fcf);

    size_t p = 2;
    /* sequence number present unless the 2015 "seq suppression" bit is set;
     * for ACK/DATA in the common case it's present. */
    if (meta->len > p) { o->seq = mpdu[p]; p += 1; }

    /* Destination addressing */
    if (dmode != HC_ADDR_NONE) {
        if (p + 2 > meta->len) goto done;
        o->dst_pan = (uint16_t)mpdu[p] | ((uint16_t)mpdu[p+1] << 8); p += 2;
        if (dmode == HC_ADDR_SHORT) {
            if (p + 2 > meta->len) goto done;
            uint16_t a = (uint16_t)mpdu[p] | ((uint16_t)mpdu[p+1] << 8); p += 2;
            fmt_short(o->dst, sizeof(o->dst), a);
        } else if (dmode == HC_ADDR_EXT) {
            if (p + 8 > meta->len) goto done;
            fmt_ext(o->dst, sizeof(o->dst), &mpdu[p]); p += 8;
        }
    }
    /* Source addressing */
    if (smode != HC_ADDR_NONE) {
        if (!pan_compr) {
            if (p + 2 > meta->len) goto done;
            o->src_pan = (uint16_t)mpdu[p] | ((uint16_t)mpdu[p+1] << 8); p += 2;
        } else {
            o->src_pan = o->dst_pan; /* compressed: shares dst PAN */
        }
        if (smode == HC_ADDR_SHORT) {
            if (p + 2 > meta->len) goto done;
            uint16_t a = (uint16_t)mpdu[p] | ((uint16_t)mpdu[p+1] << 8); p += 2;
            fmt_short(o->src, sizeof(o->src), a);
        } else if (smode == HC_ADDR_EXT) {
            if (p + 8 > meta->len) goto done;
            fmt_ext(o->src, sizeof(o->src), &mpdu[p]); p += 8;
        }
    }

done:
    /* type label + stack guess */
    switch (o->type) {
    case HC_FT_BEACON: o->type_str = "BEACON"; o->stack = HC_STACK_ZIGBEE; break;
    case HC_FT_DATA:   o->type_str = "DATA";
                       o->stack = o->security ? HC_STACK_THREAD : HC_STACK_UNKNOWN; break;
    case HC_FT_ACK:    o->type_str = "ACK";  break;
    case HC_FT_MAC_CMD:
        o->type_str = (p < meta->len) ? cmd_name(mpdu[p]) : "CMD";
        break;
    default:           o->type_str = "?";   break;
    }
    return true;
}

/* ---- event sinks --------------------------------------------------------- */
void hc_explorer_on_frame(const hc_frame_hdr_t *meta, const uint8_t *mpdu)
{
    s_count++;
    if (s_capturing) hc_storage_pcap_write(meta, mpdu);

    hc_dissected_t row;
    if (hc_dissect(meta, mpdu, &row) && s_cb.on_row)
        s_cb.on_row(&row, s_cb.ctx);
}
void hc_explorer_on_energy(const hc_energy_result_t *e)
{
    if (s_cb.on_energy) s_cb.on_energy(e, s_cb.ctx);
}
void hc_explorer_on_scan(const hc_scan_result_t *s)
{
    if (s_cb.on_scan) s_cb.on_scan(s, s_cb.ctx);
}

/* ---- capture control ----------------------------------------------------- */
esp_err_t hc_explorer_capture_start(char *path_out, size_t path_cap)
{
    if (s_capturing) return ESP_ERR_INVALID_STATE;
    esp_err_t e = hc_storage_pcap_open(path_out, path_cap);
    if (e == ESP_OK) { s_capturing = true; ESP_LOGI(TAG, "capture -> %s", path_out); }
    return e;
}
esp_err_t hc_explorer_capture_stop(void)
{
    if (!s_capturing) return ESP_OK;
    s_capturing = false;
    return hc_storage_pcap_close();
}
bool     hc_explorer_is_capturing(void) { return s_capturing; }
uint32_t hc_explorer_frame_count(void)  { return s_count; }

esp_err_t hc_explorer_init(const hc_explorer_callbacks_t *cb)
{
    if (cb) s_cb = *cb;
    s_count = 0; s_capturing = false;
    return ESP_OK;
}
