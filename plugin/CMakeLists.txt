cmake_minimum_required(VERSION 3.21)

project(AudioXL
        VERSION 0.1.1
        DESCRIPTION "AudioXL native half: registers mods WAVs in the engine's custom-sound registry without REDmod"
        LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../RedIMGRetriever/deps/RED4ext.SDK red4ext_sdk)
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../RedIMGRetriever/deps/RedLib redlib)

configure_file(src/Config.h.in ${CMAKE_CURRENT_BINARY_DIR}/generated/Config.h)

file(GLOB SOURCES CONFIGURE_DEPENDS src/*.cpp src/*.hpp)

add_library(AudioXL SHARED ${SOURCES})

target_include_directories(AudioXL PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
        ${CMAKE_CURRENT_SOURCE_DIR}/../common
        ${CMAKE_CURRENT_SOURCE_DIR}/deps
        ${CMAKE_CURRENT_BINARY_DIR}/generated)

target_link_libraries(AudioXL PRIVATE
        RED4ext.SDK
        RedLib)

target_compile_definitions(AudioXL PRIVATE
        WIN32_LEAN_AND_MEAN
        NOMINMAX
        _CRT_SECURE_NO_WARNINGS)

set_target_properties(AudioXL PROPERTIES
        PREFIX ""
        OUTPUT_NAME "AudioXL")
