cmake_minimum_required(VERSION 4.1)
project(ops1_shared C)

set(CMAKE_C_STANDARD 11)

add_executable(ops1_shared lesha/1day/main.c
        lesha/1day/prog1.c
        lesha/1day/prog4.c)
