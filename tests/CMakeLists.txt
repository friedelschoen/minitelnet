find_package(PkgConfig REQUIRED)
pkg_check_modules(CHECK REQUIRED IMPORTED_TARGET check)

add_custom_target(minitelnet_tests)

function(add_minitelnet_test name)
    add_executable(test_${name}
        ${ARGN}
    )

    target_link_libraries(test_${name} PRIVATE
        minitelnet
        PkgConfig::CHECK
    )

    add_test(
        NAME ${name}
        COMMAND test_${name}
    )

    add_dependencies(minitelnet_tests test_${name})
endfunction()

add_minitelnet_test(parser test_parser.c)
add_minitelnet_test(negotiation test_negotiation.c)
add_minitelnet_test(send test_send.c)
