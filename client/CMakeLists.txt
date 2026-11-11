cmake_minimum_required(VERSION 3.16)

# Standalone project: можно открыть папку client/ напрямую в Qt Creator.
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    project(akinator-client LANGUAGES CXX)
    set(CMAKE_CXX_STANDARD 17)
    set(CMAKE_CXX_STANDARD_REQUIRED ON)
    set(CMAKE_CXX_EXTENSIONS OFF)
    set(CMAKE_AUTOMOC ON)
    set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
    set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
    set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
    find_package(Qt6 REQUIRED COMPONENTS Core Network Widgets)
    if(NOT TARGET gamecore)
        add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../gamecore ${CMAKE_BINARY_DIR}/gamecore)
    endif()
    if(NOT TARGET akinator_protocol)
        add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../common ${CMAKE_BINARY_DIR}/common)
    endif()
endif()

set(CMAKE_AUTORCC ON)

add_executable(akinator-client
    main.cpp
    connection.cpp
    connection.h
    mainwindow.cpp
    mainwindow.h
    resources.qrc
)

target_include_directories(akinator-client PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

target_link_libraries(akinator-client
    PRIVATE
        Qt6::Core
        Qt6::Network
        Qt6::Widgets
        akinator_protocol
        gamecore
)

if(APPLE)
    set_target_properties(akinator-client PROPERTIES BUILD_RPATH "@loader_path" INSTALL_RPATH "@loader_path")
elseif(UNIX)
    set_target_properties(akinator-client PROPERTIES BUILD_RPATH "$ORIGIN" INSTALL_RPATH "$ORIGIN")
endif()
