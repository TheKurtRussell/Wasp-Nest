idf_component_register(
    SRCS "hc_radio_wifi_sniffer.c"
    INCLUDE_DIRS "."
    REQUIRES esp_wifi esp_timer
    PRIV_REQUIRES freertos
)
# Put the shared ../../common headers (hc_link_protocol.h) on the include path,
# exactly as the other hc_* components do.
target_include_directories(${COMPONENT_LIB} PUBLIC "${CMAKE_SOURCE_DIR}/../common")
