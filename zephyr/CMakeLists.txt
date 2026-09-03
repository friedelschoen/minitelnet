zephyr_library()

zephyr_library_sources_ifdef(
    CONFIG_MINITELNET
    ../src/minitelnet.c
)

zephyr_include_directories(
    ../include
)
