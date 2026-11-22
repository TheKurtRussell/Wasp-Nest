# Wasp Nest — C6 radio-service firmware (ESP32-C6)
# Build:  idf.py set-target esp32c6 && idf.py build
cmake_minimum_required(VERSION 3.16)

# Make the shared ../common headers visible to all components.
set(EXTRA_COMPONENT_DIRS "${CMAKE_CURRENT_LIST_DIR}/components")

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
idf_build_set_property(COMPILE_OPTIONS "-w" APPEND)
project(waspnest-c6)
