idf_component_register(
    SRCS "hc_wifi_recon.c"
    INCLUDE_DIRS "."
    REQUIRES esp_timer
    PRIV_REQUIRES freertos
)
# Shared wire protocol header (hc_link_protocol.h), as the other hc_* components.
target_include_directories(${COMPONENT_LIB} PUBLIC "${CMAKE_SOURCE_DIR}/../common")
