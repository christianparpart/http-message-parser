cmake_minimum_required(VERSION 3.16 FATAL_ERROR)
project(HttpMessageParser VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT WIN32 AND NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE RelWithDebInfo CACHE STRING "Choose the build mode." FORCE)
    set_property(CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS Debug Release MinSizeRel RelWithDebInfo)
endif()

# ----------------------------------------------------------------------------

add_library(HttpMessageParser STATIC
    HttpMessageParser.cpp HttpMessageParser.h
)

add_executable(test-http-message-parser
    HttpMessageParser_test.cpp
)

find_package(Catch2 REQUIRED)
target_link_libraries(test-http-message-parser
    PRIVATE
        Catch2::Catch2
        Catch2::Catch2WithMain
        HttpMessageParser
)
add_test(NAME test-http-message-parser COMMAND test-http-message-parser)
enable_testing()
