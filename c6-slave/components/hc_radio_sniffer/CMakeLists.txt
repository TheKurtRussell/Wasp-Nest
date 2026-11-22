idf_component_register(
    SRCS "hc_radio_sniffer.c"
    INCLUDE_DIRS "."
    REQUIRES ieee802154 esp_timer esp_hw_support
    PRIV_REQUIRES freertos
)
# The shared protocol/defs headers live in ../../../common and are added to the
# include path by the top-level CMakeLists via EXTRA_COMPONENT_DIRS + a shim.
target_include_directories(${COMPONENT_LIB} PUBLIC "${CMAKE_SOURCE_DIR}/../common")
