cmake_minimum_required(VERSION 3.16)

project(akinator-client LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_AUTOMOC ON)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

if(MSVC)
    add_compile_options(/utf-8)
endif()

find_package(Qt6 REQUIRED COMPONENTS Core Network Widgets)

add_subdirectory(gamecore)
add_subdirectory(common)
add_subdirectory(client)

add_custom_target(package-client
    COMMAND ${CMAKE_COMMAND} -E remove_directory ${CMAKE_BINARY_DIR}/dist/client
    COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_BINARY_DIR}/dist/client
    COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_FILE:akinator-client> $<TARGET_FILE:gamecore>
            ${CMAKE_BINARY_DIR}/dist/client
    DEPENDS akinator-client gamecore
    COMMENT "Собираю клиентскую папку dist/client")

