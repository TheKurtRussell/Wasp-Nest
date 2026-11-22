/*
 * Wasp Nest — C6 radio-service firmware entry point
 *
 * The C6 is a headless radio co-processor. All it does is start the mode agent,
 * which brings up the link to the P4 and then serves mode/sniff/scan commands.
 */
#include "esp_log.h"
#include "hc_mode_agent.h"

#define TAG "c6_main"

void app_main(void)
{
    ESP_LOGI(TAG, "Wasp Nest C6 radio service 0.1.0");
    ESP_ERROR_CHECK(hc_mode_agent_start());
    /* hc_mode_agent_start() either parks in Wi-Fi mode or returns after
     * spawning the link/sniffer tasks; nothing else to do here. */
}
