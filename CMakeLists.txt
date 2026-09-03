cmake_minimum_required(VERSION 3.20)

project(minitelnet LANGUAGES C)

add_library(minitelnet
    src/minitelnet.c
)

target_include_directories(minitelnet
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
)

target_compile_features(minitelnet PUBLIC c_std_11)

add_library(minitelnet::minitelnet ALIAS minitelnet)
