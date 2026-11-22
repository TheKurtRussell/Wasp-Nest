idf_component_register(
    SRCS "hc_storage.c"
    INCLUDE_DIRS "."
    REQUIRES fatfs sdmmc esp_driver_sdmmc
)
target_include_directories(${COMPONENT_LIB} PUBLIC "${CMAKE_SOURCE_DIR}/../common")
