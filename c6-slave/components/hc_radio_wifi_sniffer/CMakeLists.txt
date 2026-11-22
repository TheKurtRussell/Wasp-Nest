idf_component_register(
    SRCS "hc_radio_wifi_sniffer.c"
    INCLUDE_DIRS "."
    REQUIRES esp_wifi esp_timer
    PRIV_REQUIRES freertos
)
# The shared wire protocol header (hc_link_protocol.h) comes in via the project's
# EXTRA_COMPONENT_DIRS / common include path, same as the other hc_* components.
