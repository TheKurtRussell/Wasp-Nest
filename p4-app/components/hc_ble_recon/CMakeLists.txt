idf_component_register(
    SRCS "hc_ble_recon.c"
    INCLUDE_DIRS "."
    REQUIRES esp_timer
    PRIV_REQUIRES freertos
)
target_include_directories(${COMPONENT_LIB} PUBLIC "${CMAKE_SOURCE_DIR}/../common")
