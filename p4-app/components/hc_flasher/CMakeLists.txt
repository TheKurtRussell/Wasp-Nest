idf_component_register(
    SRCS "hc_flasher.c" "hc_esp_loader.c"
    INCLUDE_DIRS "."
    PRIV_REQUIRES freertos
)
target_include_directories(${COMPONENT_LIB} PUBLIC "${CMAKE_SOURCE_DIR}/../common")
# Phase 2 (USB host transport): the managed components are pulled in via
# main/idf_component.yml:
#   espressif/usb_host_vcp, usb_host_cdc_acm, usb_host_cp210x_vcp,
#   usb_host_ch34x_vcp, usb_host_ftdi_vcp
# then add: REQUIRES usb_host_vcp usb (and bind them in vcp_open()).
