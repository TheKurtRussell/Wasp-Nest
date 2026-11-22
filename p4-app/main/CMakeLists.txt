idf_component_register(
    SRCS "app_main.c"
    INCLUDE_DIRS "."
    REQUIRES hc_board hc_storage hc_explorer hc_mode_manager hc_flasher hc_ui
             hc_c6updater nvs_flash
)
