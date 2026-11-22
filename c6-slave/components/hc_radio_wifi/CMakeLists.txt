idf_component_register(
    SRCS "hc_radio_wifi.c"
    INCLUDE_DIRS "."
    PRIV_REQUIRES freertos
)
# Phase 2: add REQUIRES esp_hosted / esp_wifi here when option (A) or (B) lands.
