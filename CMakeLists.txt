cmake_minimum_required(VERSION 3.28.3)
project(Solution LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(NOT CMAKE_SYSTEM_NAME STREQUAL "Linux")
    message(FATAL_ERROR "This project is configured for Linux only")
endif()

set(LIBS_DIR ${CMAKE_SOURCE_DIR}/libs)

add_subdirectory(Client)
add_subdirectory(ProcessingServer)
add_subdirectory(DisplayServer)