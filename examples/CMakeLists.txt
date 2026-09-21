add_custom_target(minitelnet_examples)

function(add_minitelnet_example name)
    add_executable(example_${name}
        ${ARGN}
    )

    target_link_libraries(example_${name} PRIVATE
        minitelnet
    )

    add_dependencies(minitelnet_examples
        example_${name}
    )
endfunction()

add_minitelnet_example(echo echo.c)

