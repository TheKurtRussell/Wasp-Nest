idf_component_register(
    SRCS "hc_link_host.c"
    INCLUDE_DIRS "."
    REQUIRES driver
    PRIV_REQUIRES freertos
)
target_include_directories(${COMPONENT_LIB} PUBLIC "${CMAKE_SOURCE_DIR}/../common")
