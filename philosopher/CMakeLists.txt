cmake_minimum_required(VERSION 3.5)

add_executable(philosopher philosopher.c)
target_link_libraries(philosopher pthread)
