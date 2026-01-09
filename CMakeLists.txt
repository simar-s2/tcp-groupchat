cmake_minimum_required(VERSION 3.22)

project(GroupChat LANGUAGES C)

add_executable(server src/server.c)
add_executable(client src/client.c)
