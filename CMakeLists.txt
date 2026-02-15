cmake_minimum_required(VERSION 3.16.0)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(Flaps_and_Speed_Display)

spiffs_create_partition_image(spiffs spiffs_data FLASH_IN_PROJECT)