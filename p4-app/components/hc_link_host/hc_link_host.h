/*
 * Wasp Nest — P4 side of the inter-chip link
 *
 * Mirror of hc_link_slave on the C6. Same wire format (magic + hdr + CRC16),
 * same UART-first / SDIO-later backend story. The P4 is the initiator: it sends
 * HELLO on startup and matches HC_PROTO_VERSION before trusting the C6.
 */
#ifndef HC_LINK_HOST_H
#define HC_LINK_HOST_H

#include <stdint.h>
#include "esp_err.h"
#include "hc_link_protocol.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*hc_link_rx_cb_t)(hc_opcode_t op, const uint8_t *payload,
                                uint16_t len, void *ctx);

esp_err_t hc_link_host_init(hc_link_rx_cb_t cb, void *ctx);
esp_err_t hc_link_host_send(hc_opcode_t op, const void *payload, uint16_t len);

/* Blocking HELLO handshake; fills out the C6's reported identity. Returns
 * ESP_ERR_INVALID_VERSION on a proto mismatch. */
esp_err_t hc_link_host_handshake(hc_hello_t *c6_out, uint32_t timeout_ms);

#ifdef __cplusplus
}
#endif
#endif /* HC_LINK_HOST_H */
