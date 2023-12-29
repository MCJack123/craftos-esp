# For more information about build system see
# https://docs.espressif.com/projects/esp-idf/en/latest/api-guides/build-system.html
# The following five lines of boilerplate have to be in your project's
# CMakeLists in this exact order for cmake to work correctly
cmake_minimum_required(VERSION 3.16)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(main)

#littlefs_create_partition_image(rom craftos2-rom/rom FLASH_IN_PROJECT)
partition_table_get_partition_info(offset "--partition-name rom" "offset")
esptool_py_flash_target_image(flash "rom" "${offset}" "${CMAKE_BINARY_DIR}/rom-mmfs.bin")
