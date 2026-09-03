cmake_minimum_required(VERSION 3.20)

project(minitelnet LANGUAGES C)

option(MINITELNET_WERROR "Treat minitelnet warnings as errors" OFF)

add_library(minitelnet
    src/minitelnet.c
)

target_include_directories(minitelnet
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
)

target_compile_options(minitelnet PRIVATE
    -Wall
    -Wextra
    -Wpedantic
)

if(MINITELNET_WERROR)
    target_compile_options(minitelnet PRIVATE -Werror)
endif()

target_compile_features(minitelnet PUBLIC c_std_11)

add_library(minitelnet::minitelnet ALIAS minitelnet)

