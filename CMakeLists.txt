cmake_minimum_required (VERSION 2.8)
project (OS)
add_subdirectory(matrix)
add_subdirectory(mystring)
add_subdirectory(mymenu)
add_subdirectory(process)
add_subdirectory(policeandthief)
add_subdirectory(philosopher)
add_subdirectory(myshell)
add_subdirectory(memorymanage)
add_executable(startproject startproject.c)
add_custom_command(TARGET startproject POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${PROJECT_SOURCE_DIR}/useroperation"
        $<TARGET_FILE_DIR:startproject>/useroperation)

add_custom_command(TARGET startproject POST_BUILD
    COMMAND chmod +x useroperation/*.sh)
