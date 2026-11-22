idf_component_register(
    SRCS "hc_explorer.c"
    INCLUDE_DIRS "."
    REQUIRES hc_storage
)
target_include_directories(${COMPONENT_LIB} PUBLIC "${CMAKE_SOURCE_DIR}/../common")
