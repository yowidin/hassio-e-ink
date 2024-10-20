cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(power-ic)

target_include_directories(app PRIVATE include)

target_sources(app PRIVATE
        src/led.cpp
        src/main.cpp
        src/power.cpp
        src/shutdown.cpp
)
