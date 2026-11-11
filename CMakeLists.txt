cmake_minimum_required(VERSION 3.16)

project(akinator LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_AUTOMOC ON)

option(AKINATOR_BUILD_CLIENT "Build Qt GUI client" ON)
option(AKINATOR_BUILD_SERVER "Build server" ON)
option(AKINATOR_BUILD_TESTS "Build unit tests" ON)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

if(MSVC)
    add_compile_options(/utf-8)
endif()

find_package(Qt6 REQUIRED COMPONENTS Core Network)
if(AKINATOR_BUILD_CLIENT)
    find_package(Qt6 REQUIRED COMPONENTS Widgets)
endif()
if(AKINATOR_BUILD_TESTS)
    find_package(Qt6 REQUIRED COMPONENTS Test)
endif()

add_subdirectory(gamecore)
add_subdirectory(common)
if(AKINATOR_BUILD_SERVER)
    add_subdirectory(server)
endif()
if(AKINATOR_BUILD_CLIENT)
    add_subdirectory(client)
endif()
if(AKINATOR_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

# Готовые папки для раздачи: игрокам нужен только dist/client,
# серверу — dist/server (+ Python 3 и numpy).
if(AKINATOR_BUILD_CLIENT)
    add_custom_target(package-client
        COMMAND ${CMAKE_COMMAND} -E remove_directory ${CMAKE_BINARY_DIR}/dist/client
        COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_BINARY_DIR}/dist/client
        COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_FILE:akinator-client> $<TARGET_FILE:gamecore>
                ${CMAKE_BINARY_DIR}/dist/client
        DEPENDS akinator-client gamecore
        COMMENT "Собираю клиентскую папку dist/client")
endif()

if(AKINATOR_BUILD_SERVER)
    add_custom_target(package-server
        COMMAND ${CMAKE_COMMAND} -E remove_directory ${CMAKE_BINARY_DIR}/dist/server
        COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_BINARY_DIR}/dist/server
        COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_FILE:akinator-server> $<TARGET_FILE:gamecore>
                ${CMAKE_SOURCE_DIR}/gamecore/ml_hints.py
                ${CMAKE_SOURCE_DIR}/gamecore/requirements.txt
                ${CMAKE_BINARY_DIR}/dist/server
        DEPENDS akinator-server gamecore
        COMMENT "Собираю серверную папку dist/server")
endif()
