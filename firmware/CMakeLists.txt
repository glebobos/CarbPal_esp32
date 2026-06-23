cmake_minimum_required(VERSION 3.20)

set(IDF_TARGET esp32c3)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(esp32_portal)

# Ensure spiffs_image dir exists so CMake does not error when it is empty
file(MAKE_DIRECTORY ${CMAKE_SOURCE_DIR}/spiffs_image)
file(TOUCH ${CMAKE_SOURCE_DIR}/spiffs_image/.gitkeep)

spiffs_create_partition_image(storage ${CMAKE_SOURCE_DIR}/spiffs_image FLASH_IN_PROJECT)
