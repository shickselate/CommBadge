# Top-level CMakeLists.txt for the comm_badge ESP-IDF project.
# Registers the components/ directory and names the project.

cmake_minimum_required(VERSION 3.16)

set(EXTRA_COMPONENT_DIRS components)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(comm_badge)
