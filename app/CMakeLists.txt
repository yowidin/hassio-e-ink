cmake_minimum_required(VERSION 3.13.1)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})

project(app LANGUAGES C CXX VERSION 1.0.0)

target_include_directories(app PRIVATE include)

target_sources(app PRIVATE
        src/main.cpp
        src/fuel_gauge.c
        src/settings.cpp
        src/shell.c
        src/wifi.cpp
)