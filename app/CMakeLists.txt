cmake_minimum_required(VERSION 3.13.1)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})

project(app LANGUAGES C CXX VERSION 1.0.0)

target_include_directories(app PRIVATE include)

target_sources(app PRIVATE
    src/http/server.cpp

    # Broken Zephyr APIs
    src/http/image_server_config.c
    src/http/json.c
    src/http/network_list.c
    src/http/response.c
    src/http/status.c
    src/http/wifi_config.c
    src/fuel_gauge.c

    src/display.cpp
    src/dns.cpp
    src/led.cpp
    src/main.cpp
    src/image_client.cpp
    src/settings.cpp
    src/shell.c
    src/shutdown.cpp
    src/wifi.cpp
)

# HTML Resources
set(HEI_GENERATED_DIR ${CMAKE_CURRENT_BINARY_DIR}/generated/)
set(HEI_GENERATED_INCLUDE_DIR ${HEI_GENERATED_DIR}/hei/)

# Bundle resources
set(HEI_RES_FILES
    index.html
    bootstrap.bundle.min.js
    bootstrap.min.css
)
foreach(res_file ${HEI_RES_FILES})
    generate_inc_file_for_target(app
        res/static/${res_file}
        ${HEI_GENERATED_INCLUDE_DIR}/http/static/${res_file}.gz.inc --gzip)
endforeach()

target_include_directories(app PRIVATE ${HEI_GENERATED_DIR})

# HTTP Server boilerplate
# target_link_libraries(app PRIVATE zephyr_interface zephyr)

zephyr_linker_sources(SECTIONS ld/http-server-sections.ld)
zephyr_linker_section(NAME http_resource_desc_http_server_service
    KVMA RAM_REGION GROUP RODATA_REGION SUBALIGN Z_LINK_ITERABLE_SUBALIGN)

# Add sunflower RAM and ROM reports
foreach(report ram rom)
    add_custom_target(
        ${report}_sunflower
        ${PYTHON_EXECUTABLE}
        ${CMAKE_CURRENT_SOURCE_DIR}/../scripts/footprint
        -b ${CMAKE_CURRENT_BINARY_DIR}
        -t ${report}
        --skip-build
        --html ${report}.html
        DEPENDS ${report}_report
        USES_TERMINAL
        WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
    )
endforeach()