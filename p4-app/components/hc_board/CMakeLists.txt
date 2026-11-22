idf_component_register(
    SRCS "hc_board.c"
    INCLUDE_DIRS "."
    REQUIRES driver esp_driver_gpio m5stack_tab5
    PRIV_REQUIRES freertos
)
# m5stack_tab5 (added via main/idf_component.yml) provides bsp/esp-bsp.h and
# pulls in esp_lvgl_port + lvgl + the panel/touch drivers.
