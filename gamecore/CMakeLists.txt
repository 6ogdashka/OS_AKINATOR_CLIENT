cmake_minimum_required(VERSION 3.16)

add_library(gamecore SHARED
    src/gamecore.cpp
)

target_compile_definitions(gamecore PRIVATE GAMECORE_BUILD)

target_include_directories(gamecore
    PUBLIC  ${CMAKE_CURRENT_SOURCE_DIR}/include
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src
)

if(NOT MSVC)
    set_target_properties(gamecore PROPERTIES
        CXX_VISIBILITY_PRESET hidden
        VISIBILITY_INLINES_HIDDEN ON
    )
endif()
