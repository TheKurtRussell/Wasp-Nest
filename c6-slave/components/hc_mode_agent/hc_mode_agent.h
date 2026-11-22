/*
 * Wasp Nest — C6 mode agent
 *
 * The C6's top-level coordinator. Receives commands from the P4 over the link,
 * drives the radio between IDLE / SNIFFER / WIFI, and pushes capture results and
 * events back up. This is the only component the C6 main() needs to start.
 *
 * Mode-switch policy (matches the "C6 reboots between roles" decision):
 *   - IDLE  <-> SNIFFER: a soft switch. We tear down/bring up esp_ieee802154
 *     in place; no reboot needed.
 *   - any  ->  WIFI: handing the radio to esp_hosted. Because esp_hosted owns
 *     Wi-Fi init and its own SDIO slave task, the clean + robust path is to
 *     reboot the C6 into the hosted-slave image. The P4 triggers this by
 *     holding the C6 in reset and re-releasing with a boot flag in RTC memory
 *     (see docs/protocol.md "Mode switching"). hc_mode_agent persists the
 *     requested mode to RTC and calls esp_restart() so the next boot comes up
 *     directly in the right role.
 */
#ifndef HC_MODE_AGENT_H
#define HC_MODE_AGENT_H

#include "esp_err.h"
#include "hc_link_protocol.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Starts link + restores persisted mode. Blocks only briefly; runs its own
 * tasks thereafter. */
esp_err_t hc_mode_agent_start(void);

/* Current active mode (for diagnostics). */
hc_radio_mode_t hc_mode_agent_current(void);

#ifdef __cplusplus
}
#endif
#endif /* HC_MODE_AGENT_H */
