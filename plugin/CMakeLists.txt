cmake_minimum_required(VERSION 3.21)

project(AudioXL
        VERSION 0.1.4
        DESCRIPTION "AudioXL native half: registers mods WAVs in the engine's custom-sound registry without REDmod"
        LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(FetchContent)

set(RED4EXT_SDK_DIR "" CACHE PATH "Existing RED4ext.SDK checkout (empty = search, then fetch)")
set(REDLIB_DIR "" CACHE PATH "Existing RedLib checkout (empty = search, then fetch)")

set(RED4EXT_SDK_HINTS
        "${RED4EXT_SDK_DIR}"
        "${CMAKE_CURRENT_SOURCE_DIR}/deps/RED4ext.SDK"
        "${CMAKE_CURRENT_SOURCE_DIR}/../RED4ext.SDK"
        )
set(REDLIB_HINTS
        "${REDLIB_DIR}"
        "${CMAKE_CURRENT_SOURCE_DIR}/deps/RedLib"
        "${CMAKE_CURRENT_SOURCE_DIR}/../RedLib"
        )

set(RED4EXT_SDK_FOUND "")
foreach(hint IN LISTS RED4EXT_SDK_HINTS)
    if(EXISTS "${hint}/CMakeLists.txt")
        set(RED4EXT_SDK_FOUND "${hint}")
        break()
    endif()
endforeach()
set(REDLIB_FOUND "")
foreach(hint IN LISTS REDLIB_HINTS)
    if(EXISTS "${hint}/CMakeLists.txt")
        set(REDLIB_FOUND "${hint}")
        break()
    endif()
endforeach()

if(RED4EXT_SDK_FOUND)
    message(STATUS "AudioXL: RED4ext.SDK from ${RED4EXT_SDK_FOUND}")
    add_subdirectory("${RED4EXT_SDK_FOUND}" red4ext_sdk)
else()
    message(STATUS "AudioXL: fetching RED4ext.SDK")
    FetchContent_Declare(red4ext_sdk
            GIT_REPOSITORY https://github.com/WopsS/RED4ext.SDK.git
            GIT_TAG cce6e255f5e61056e634f324a3dbdfa6da523af9)
    FetchContent_MakeAvailable(red4ext_sdk)
endif()

if(REDLIB_FOUND)
    message(STATUS "AudioXL: RedLib from ${REDLIB_FOUND}")
    add_subdirectory("${REDLIB_FOUND}" redlib)
else()
    message(STATUS "AudioXL: fetching RedLib")
    FetchContent_Declare(redlib
            GIT_REPOSITORY https://github.com/psiberx/cp2077-red-lib.git
            GIT_TAG 009d404d0ed96c7bfce8e937cc1cc97cc13b1c5f)
    FetchContent_MakeAvailable(redlib)
endif()

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
