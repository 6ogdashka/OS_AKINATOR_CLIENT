cmake_minimum_required(VERSION 3.16)

add_library(gamecore SHARED
    src/gamecore.cpp
)

target_compile_definitions(gamecore PRIVATE GAMECORE_BUILD
    AKINATOR_ML_SCRIPT_PATH="${CMAKE_CURRENT_SOURCE_DIR}/ml_hints.py")

target_link_libraries(gamecore PRIVATE Qt6::Core)

add_custom_command(TARGET gamecore POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory $<TARGET_FILE_DIR:gamecore>
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            ${CMAKE_CURRENT_SOURCE_DIR}/ml_hints.py
            $<TARGET_FILE_DIR:gamecore>/ml_hints.py)

file(MAKE_DIRECTORY ${CMAKE_RUNTIME_OUTPUT_DIRECTORY})
configure_file(${CMAKE_CURRENT_SOURCE_DIR}/ml_hints.py
               ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/ml_hints.py COPYONLY)

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
