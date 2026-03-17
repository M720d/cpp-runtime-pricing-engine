cmake_minimum_required(VERSION 3.15)
project(pricing_core LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Let pybind11 help find the right Python
set(pybind11_DIR "/opt/miniconda3/lib/python3.13/site-packages/pybind11/share/cmake/pybind11")

# Find pybind11 installed in your conda env
find_package(pybind11 CONFIG REQUIRED)

# Build the Python extension module "pricing_core"
pybind11_add_module(pricing_core
    src/bindings.cpp
    src/pricing_core.cpp
)

# Include our headers
target_include_directories(pricing_core PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)

# ---------------------------------------------------------------------------
# Compiler optimisation flags
# ---------------------------------------------------------------------------
# Release builds: -O3 for maximum speed, -march=native to use all CPU features
#   (AVX, FMA, etc.) available on the build machine.
# Debug builds:   -O0 disables optimisation so debugger shows actual source lines;
#                 -g emits debug symbols.
# The $<CONFIG:...> generator expressions activate flags only for the matching
# build type (cmake --build . --config Release vs Debug).
# ---------------------------------------------------------------------------
target_compile_options(pricing_core PRIVATE
    $<$<CONFIG:Release>:-O3 -march=native>
    $<$<CONFIG:Debug>:-O0 -g>
)

# ---------------------------------------------------------------------------
# Custom 'test' target — runs pytest after the module is built
# ---------------------------------------------------------------------------
# Usage:  /opt/miniconda3/bin/cmake --build build --target test
#
# find_program searches PATH for the pytest executable.
# The DEPENDS clause ensures pricing_core.so is rebuilt before tests run.
# WORKING_DIRECTORY is set to build/ so the .so is on the import path
# alongside conftest.py's sys.path manipulation.
# ---------------------------------------------------------------------------
find_program(PYTEST_EXECUTABLE pytest HINTS /opt/miniconda3/bin)
if(PYTEST_EXECUTABLE)
    add_custom_target(run_tests
        COMMAND ${PYTEST_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tests/ -v
        WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
        DEPENDS pricing_core
        COMMENT "Running pytest test suite in tests/"
    )
endif()





