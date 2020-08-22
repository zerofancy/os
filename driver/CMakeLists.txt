cmake_minimum_required(VERSION 3.5)

aux_source_directory(src SRC_DIR)
add_executable(drivertest ${SRC_DIR})
