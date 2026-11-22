/*
 * Wasp Nest — C6 side of the inter-chip link
 *
 * Owns the physical transport to the P4 and does framing (magic + hdr + CRC16).
 * Two transport backends are provided behind one API, chosen at build time:
 *
 *   HC_LINK_TRANSPORT_UART  (default for bring-up)
 *       A plain UART on two spare M5-Bus pins. Dead simple, easy to scope,
 *       works while you are still debugging. ~1 Mbaud is plenty for sniffing.
 *
 *   HC_LINK_TRANSPORT_SDIO  (phase 2)
 *       Rides the same SDIO2 bus esp_hosted uses, via a custom RPC channel, so
 *       no extra wires. Higher throughput, needed once WiFi mode shares the bus.
 *       See docs/protocol.md for why we start on UART.
 *
 * The P4 and C6 must agree on the backend. Keep them in sync via a shared
 * Kconfig (see sdkconfig.defaults in both projects).
 */
#ifndef HC_LINK_SLAVE_H
#define HC_LINK_SLAVE_H

#include <stdint.h>
#include "esp_err.h"
#include "hc_link_protocol.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Invoked for every well-formed frame received from the P4. Runs on the link
 * RX task. The handler must not block for long. */
typedef void (*hc_link_rx_cb_t)(hc_opcode_t op, const uint8_t *payload,
                                uint16_t len, void *ctx);

esp_err_t hc_link_slave_init(hc_link_rx_cb_t cb, void *ctx);

/* Send one framed message to the P4. Thread-safe. */
esp_err_t hc_link_slave_send(hc_opcode_t op, const void *payload, uint16_t len);

/* Convenience: ship a log line to the P4 (shows in the on-device console). */
void hc_link_slave_logf(uint8_t level, const char *fmt, ...);

#ifdef __cplusplus
}
#endif
#endif /* HC_LINK_SLAVE_H */
