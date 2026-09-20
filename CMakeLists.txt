cmake_minimum_required(VERSION 3.20)

project(minitelnet LANGUAGES C)

option(MINITELNET_WERROR "Treat minitelnet warnings as errors" OFF)

add_library(minitelnet
    minitelnet.c
)

target_include_directories(minitelnet
    PUBLIC . 
)

target_compile_options(minitelnet PRIVATE
    -Wall
    -Wextra
    -Wpedantic
)

set_property(TARGET minitelnet PROPERTY C_STANDARD 90)

if(MINITELNET_WERROR)
    target_compile_options(minitelnet PRIVATE -Werror)
endif()

add_library(minitelnet::minitelnet ALIAS minitelnet)

