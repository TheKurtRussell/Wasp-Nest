idf_component_register(
    SRCS "hc_mode_manager.c"
    INCLUDE_DIRS "."
    REQUIRES hc_link_host hc_explorer hc_board
    PRIV_REQUIRES freertos
)
target_include_directories(${COMPONENT_LIB} PUBLIC "${CMAKE_SOURCE_DIR}/../common")
