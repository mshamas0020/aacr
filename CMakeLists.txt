cmake_minimum_required(VERSION 3.19)

project(aacr)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED TRUE)

set(SRC_DIR ${CMAKE_SOURCE_DIR}/src)
set(FDK_AAC_DIR ${CMAKE_SOURCE_DIR}/../fdk-aac)

find_package(fdk-aac CONFIG REQUIRED)
link_libraries(fdk-aac)

if(NOT ${fdk-aac_FOUND})
    message(FATAL_ERROR "Cannot find libfdk-aac.")
else()
    message("Found libfdk-aac at " ${fdk-aac_DIR} ".")
endif()

include_directories(${SRC_DIR})

add_executable(aacr_enc ${SRC_DIR}/enc.cpp)
add_executable(aacr_dec ${SRC_DIR}/dec.cpp)