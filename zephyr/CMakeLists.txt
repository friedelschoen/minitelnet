zephyr_library()

zephyr_library_sources_ifdef(
    CONFIG_MINITELNET
    ../minitelnet.c
)

zephyr_include_directories(
    ..
)
