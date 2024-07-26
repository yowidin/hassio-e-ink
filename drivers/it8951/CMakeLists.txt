zephyr_library_named(IT8951)

zephyr_library_include_directories(include/private)
zephyr_include_directories(include/public)

zephyr_library_sources(
        src/it8951.cpp
        src/driver.c
)
