cmake_minimum_required(VERSION 3.18)
project(chat C)

set(CMAKE_C_STANDARD 99)

add_executable(chat main.c)
add_executable(client client.c)
add_executable(server server.c)
set(CMAKE_C_FLAGS "${CMAKE_CFLAGS} -pthread")
