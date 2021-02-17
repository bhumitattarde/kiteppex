cmake_minimum_required(VERSION 3.10)
project(kiteppex) 

#detect Linux (for optional linking of libuv)
if(UNIX AND NOT APPLE)
        set(LINUX TRUE)
endif()

#find openSSL
find_package(OpenSSL REQUIRED)

#find zlib
find_package(ZLIB REQUIRED)

#find libuv (optional on Linux)
if(DEFINED LINUX)
    find_library(UV_LIB uv)
    find_path(UV_INCLUDE "uv.h")
else()
    find_library(UV_LIB uv REQUIRED)
    find_path(UV_INCLUDE "uv.h" REQUIRED)
endif()

#find uWS
find_library(UWS_LIB uWS REQUIRED)
find_path(UWS_INCLUDE uWS REQUIRED)

#compile & link

add_executable(ex "${CMAKE_SOURCE_DIR}/main.cpp")

if((NOT UV_LIB OR NOT UV_INCLUDE) AND DEFINED LINUX)
    message("Couldn't find libuv..\nProceding anyway since Linux was detected..")
    target_include_directories(ex PUBLIC ${UWS_INCLUDE} ${CMAKE_SOURCE_DIR}/include)
    target_link_libraries(ex PUBLIC pthread OpenSSL::SSL OpenSSL::Crypto ZLIB::ZLIB ${UWS_LIB})
else()
    target_include_directories(ex PUBLIC ${UWS_INCLUDE} ${UV_INCLUDE} ${CMAKE_SOURCE_DIR}/include)
    target_link_libraries(ex PUBLIC pthread OpenSSL::SSL OpenSSL::Crypto ZLIB::ZLIB ${UWS_LIB} ${UV_LIB})
endif()

