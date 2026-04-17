cmake_minimum_required(VERSION 3.15)
project(ClaudeCodePlugin VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_library(ClaudeCodePlugin SHARED
    ClaudeCodePlugin.cpp
    ClaudeCodePlugin.def
)

target_include_directories(ClaudeCodePlugin PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

target_compile_definitions(ClaudeCodePlugin PRIVATE TMPLUGIN_EXPORTS)

target_link_options(ClaudeCodePlugin PRIVATE "/DEF:${CMAKE_CURRENT_SOURCE_DIR}/ClaudeCodePlugin.def")
