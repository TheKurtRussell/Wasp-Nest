/*
 * Wasp Nest — shared IEEE 802.15.4 MAC-layer definitions
 *
 * Minimal frame-control-field decode shared by the C6 (for cheap stack
 * guessing during scans) and the P4 (for the on-screen dissector). This is
 * deliberately small — full dissection lives in hc_explorer on the P4.
 *
 * Reference: IEEE 802.15.4-2015, clause 7 (MAC frame formats).
 */
#ifndef HC_IEEE802154_DEFS_H
#define HC_IEEE802154_DEFS_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Frame Control Field (FCF) is the first 2 bytes of the MHR, little-endian. */
#define HC_FCF_TYPE_MASK      0x0007
#define HC_FCF_SEC_EN         0x0008
#define HC_FCF_FRAME_PENDING  0x0010
#define HC_FCF_ACK_REQ        0x0020
#define HC_FCF_PAN_ID_COMPR   0x0040
#define HC_FCF_DST_ADDR_SHIFT 10
#define HC_FCF_DST_ADDR_MASK  0x0C00
#define HC_FCF_VER_SHIFT      12
#define HC_FCF_VER_MASK       0x3000
#define HC_FCF_SRC_ADDR_SHIFT 14
#define HC_FCF_SRC_ADDR_MASK  0xC000

typedef enum {
    HC_FT_BEACON   = 0,
    HC_FT_DATA     = 1,
    HC_FT_ACK      = 2,
    HC_FT_MAC_CMD  = 3,
    HC_FT_RESERVED = 4,
    HC_FT_MULTIPURPOSE = 5,
    HC_FT_FRAGMENT = 6,
    HC_FT_EXTENDED = 7,
} hc_frame_type_t;

typedef enum {
    HC_ADDR_NONE     = 0,  /* PAN id + address absent */
    HC_ADDR_RESERVED = 1,
    HC_ADDR_SHORT    = 2,  /* 16-bit */
    HC_ADDR_EXT      = 3,  /* 64-bit */
} hc_addr_mode_t;

/* MAC command identifiers we care about for dissection labels. */
typedef enum {
    HC_CMD_ASSOC_REQ        = 0x01,
    HC_CMD_ASSOC_RESP       = 0x02,
    HC_CMD_DISASSOC_NOTIFY  = 0x03,
    HC_CMD_DATA_REQ         = 0x04,
    HC_CMD_PANID_CONFLICT   = 0x05,
    HC_CMD_ORPHAN_NOTIFY    = 0x06,
    HC_CMD_BEACON_REQ       = 0x07,
    HC_CMD_COORD_REALIGN    = 0x08,
    HC_CMD_GTS_REQ          = 0x09,
} hc_mac_cmd_t;

static inline hc_frame_type_t hc_fcf_type(uint16_t fcf) {
    return (hc_frame_type_t)(fcf & HC_FCF_TYPE_MASK);
}
static inline hc_addr_mode_t hc_fcf_dst_mode(uint16_t fcf) {
    return (hc_addr_mode_t)((fcf & HC_FCF_DST_ADDR_MASK) >> HC_FCF_DST_ADDR_SHIFT);
}
static inline hc_addr_mode_t hc_fcf_src_mode(uint16_t fcf) {
    return (hc_addr_mode_t)((fcf & HC_FCF_SRC_ADDR_MASK) >> HC_FCF_SRC_ADDR_SHIFT);
}
static inline uint8_t hc_fcf_version(uint16_t fcf) {
    return (uint8_t)((fcf & HC_FCF_VER_MASK) >> HC_FCF_VER_SHIFT);
}

#ifdef __cplusplus
}
#endif
#endif /* HC_IEEE802154_DEFS_H */
