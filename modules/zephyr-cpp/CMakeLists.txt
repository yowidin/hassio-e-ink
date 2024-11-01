zephyr_library_named(zephyr_cpp)

zephyr_include_directories(include/public)

zephyr_library_sources(
    src/error.cpp
    src/mutex.cpp

    src/drivers/gpio.cpp
    src/drivers/spi.cpp
)
