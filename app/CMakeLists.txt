cmake_minimum_required(VERSION 3.13.1)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})

project(app LANGUAGES C CXX VERSION 1.0.0)

target_include_directories(app PRIVATE include)

target_sources(app PRIVATE
        src/dns.cpp
        src/fuel_gauge.c
        src/main.cpp
        src/server.cpp
        src/settings.cpp
        src/shell.c
        src/wifi.cpp
)

# HTML Resources
set(HEI_GENERATED_DIR ${CMAKE_CURRENT_BINARY_DIR}/generated/)
set(HEI_GENERATED_INCLUDE_DIR ${HEI_GENERATED_DIR}/hei/)

set(HEI_INDEX_HTML res/index.html)
generate_inc_file_for_target(app ${HEI_INDEX_HTML} ${HEI_GENERATED_INCLUDE_DIR}/http/static/index.html.gz.inc --gzip)

target_include_directories(app PRIVATE ${HEI_GENERATED_DIR})

# HTTP Server boilerplate
target_link_libraries(app PRIVATE zephyr_interface zephyr)

zephyr_linker_sources(SECTIONS ld/http-server-sections.ld)
zephyr_linker_section(NAME http_resource_desc_http_server_service
        KVMA RAM_REGION GROUP RODATA_REGION SUBALIGN Z_LINK_ITERABLE_SUBALIGN)