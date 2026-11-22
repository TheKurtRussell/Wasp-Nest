idf_component_register(
    SRCS "hc_mode_agent.c"
    INCLUDE_DIRS "."
    REQUIRES hc_link_slave hc_radio_sniffer hc_radio_wifi hc_radio_wifi_sniffer esp_system
    PRIV_REQUIRES freertos esp_timer
)
target_include_directories(${COMPONENT_LIB} PUBLIC "${CMAKE_SOURCE_DIR}/../common")
