# CMakeLists in this exact order for cmake to work correctly
cmake_minimum_required(VERSION 3.16)
set(PARTITION_TABLE_CUSTOM 1)
set(PARTITION_TABLE_CUSTOM_FILENAME "partitions.csv")
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(tinyml_ts_espidf)
