cmake_minimum_required(VERSION 3.5)

aux_source_directory(src SRC_DIR)
add_executable(mymenu ${SRC_DIR})

target_link_libraries(mymenu ncurses)
target_link_libraries(mymenu menu)
