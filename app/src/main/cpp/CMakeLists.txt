cmake_minimum_required(VERSION 3.22.1)
project(lucia)

add_library(lucia SHARED
    lucia_jni.cpp
    equip_core.c
    rubimem.c
)

find_library(log-lib log)
target_link_libraries(lucia ${log-lib})
