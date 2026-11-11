cmake_minimum_required(VERSION 3.16)

# Standalone project: можно открыть папку server/ напрямую в Qt Creator.
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    project(akinator-server LANGUAGES CXX)
    set(CMAKE_CXX_STANDARD 17)
    set(CMAKE_CXX_STANDARD_REQUIRED ON)
    set(CMAKE_CXX_EXTENSIONS OFF)
    set(CMAKE_AUTOMOC ON)
    set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
    set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
    set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
    find_package(Qt6 REQUIRED COMPONENTS Core Network)
    if(NOT TARGET gamecore)
        add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../gamecore ${CMAKE_BINARY_DIR}/gamecore)
    endif()
    if(NOT TARGET akinator_protocol)
        add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../common ${CMAKE_BINARY_DIR}/common)
    endif()
endif()

add_executable(akinator-server
    main.cpp
    server.cpp
    server.h
)

target_include_directories(akinator-server PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

target_link_libraries(akinator-server
    PRIVATE
        Qt6::Core
        Qt6::Network
        akinator_protocol
        gamecore
)

if(APPLE)
    set_target_properties(akinator-server PROPERTIES BUILD_RPATH "@loader_path" INSTALL_RPATH "@loader_path")
elseif(UNIX)
    set_target_properties(akinator-server PROPERTIES BUILD_RPATH "$ORIGIN" INSTALL_RPATH "$ORIGIN")
endif()
