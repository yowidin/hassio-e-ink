zephyr_library_named(IT8951)

zephyr_library_include_directories(include/private)
zephyr_include_directories(include/public)

zephyr_library_sources(
    src/display.cpp
    src/hal.cpp
    src/init.cpp

    src/init.c
)
