cmake_minimum_required(VERSION 3.20.0)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(us_probe_mesh_mock_node_nrf52)

target_sources(app PRIVATE
    src/main.c
    src/mesh.c
)

target_include_directories(app PRIVATE src)
