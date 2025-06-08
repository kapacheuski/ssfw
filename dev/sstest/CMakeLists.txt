cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(peripheral_nus)

target_sources(app PRIVATE
        src/board.c
        src/ble_nus.c
        src/sensors.c
        src/main.c

)
target_include_directories(app PRIVATE
        inc
)