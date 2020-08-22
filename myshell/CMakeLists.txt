cmake_minimum_required(VERSION 3.5)

aux_source_directory(src SRC_DIR)
add_executable(myshell ${SRC_DIR})
