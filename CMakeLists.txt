cmake_minimum_required(VERSION 3.20)

project(minitelnet LANGUAGES C)

include(CTest)

option(MINITELNET_WERROR "Treat minitelnet warnings as errors" ON)
option(MINITELNET_TEST_ON_BUILD "Run tests as part of the default build" ON)
option(MINITELNET_BUILD_EXAMPLES "Build minitelnet examples" ON)

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

if(BUILD_TESTING)
    add_subdirectory(tests)

    if(MINITELNET_TEST_ON_BUILD)
        add_custom_target(check ALL
            COMMAND ${CMAKE_CTEST_COMMAND} --output-on-failure
            DEPENDS minitelnet_tests
            WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
            USES_TERMINAL
        )
    endif()
endif()

if(MINITELNET_BUILD_EXAMPLES)
    add_subdirectory(examples)
endif()
