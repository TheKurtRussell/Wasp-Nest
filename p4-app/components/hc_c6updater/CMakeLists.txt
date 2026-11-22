idf_component_register(
    SRCS "hc_c6updater.c"
    INCLUDE_DIRS "."
    REQUIRES hc_board esp-serial-flasher
    PRIV_REQUIRES freertos fatfs
)
target_include_directories(${COMPONENT_LIB} PUBLIC "${CMAKE_SOURCE_DIR}/../common")
# esp-serial-flasher is pulled in via main/idf_component.yml
# (espressif/esp-serial-flasher). hc_c6updater.c compiles with or without it
# (headless fallback) via __has_include, so configure succeeds before fetch.
