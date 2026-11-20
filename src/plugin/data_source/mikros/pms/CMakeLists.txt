# Mikros PMS root: project bootstrap then cmake/ for the real target.
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    cmake_minimum_required(VERSION 3.26)
    project(pms LANGUAGES CXX)
    set(PMS_STANDALONE ON)
else()
    set(PMS_STANDALONE OFF)
endif()

set(PMS_ROOT "${CMAKE_CURRENT_SOURCE_DIR}")
include(CTest)
add_subdirectory(cmake)
