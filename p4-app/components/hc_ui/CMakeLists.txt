idf_component_register(
    SRCS "hc_ui.c"
    INCLUDE_DIRS "."
    REQUIRES hc_mode_manager hc_explorer hc_flasher m5stack_tab5 esp_timer esp_hw_support esp_app_format
)
target_include_directories(${COMPONENT_LIB} PUBLIC "${CMAKE_SOURCE_DIR}/../common")
# LVGL is pulled in as a managed component (main/idf_component.yml: lvgl/lvgl).
# hc_ui.c compiles with or without it (headless fallback) via __has_include.
