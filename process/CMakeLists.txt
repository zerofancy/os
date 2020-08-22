cmake_minimum_required(VERSION 2.8)
add_executable(fork 1_fork/fork.c)
add_executable(signal 2_signal/signal.c)
add_executable(pipe 3_pipe/pipe.c)
add_executable(popen 4_popen/popen.c)
add_executable(fifo1 5_fifo/fifo1.c)
add_executable(fifo2 5_fifo/fifo2.c)

add_custom_command(TARGET pipe POST_BUILD
    COMMAND cp ${PROJECT_SOURCE_DIR}/process/3_pipe/hello . )
