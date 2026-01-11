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





