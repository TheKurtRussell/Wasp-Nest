idf_component_register(
    SRCS "hc_radio_ble_scanner.c"
    INCLUDE_DIRS "."
    REQUIRES bt
)
# Shared wire protocol header (hc_link_protocol.h), as the other hc_* components.
target_include_directories(${COMPONENT_LIB} PUBLIC "${CMAKE_SOURCE_DIR}/../common")
