cmake_minimum_required(VERSION 3.20)

# for global installs
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})

# populates the $PROJECT_NAME var and this is what the output files are named
project(capstone)

# app is a predesignated target that zephyr uses
target_sources(app PRIVATE
    src/main.c
    src/qspi_pattern.c
    src/ppi_fork.c
    src/hfxo.c
)

target_include_directories(app PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
