cmake_minimum_required(VERSION 3.16)

add_library(akinator_protocol INTERFACE)

target_link_libraries(akinator_protocol INTERFACE Qt6::Core)
target_include_directories(akinator_protocol INTERFACE ${CMAKE_CURRENT_SOURCE_DIR})
