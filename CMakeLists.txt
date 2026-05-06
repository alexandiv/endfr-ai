cmake_minimum_required(VERSION 3.20.0)
project(endf_reader VERSION 0.1.0 LANGUAGES C CXX)

add_executable(endf_reader main.cpp)

if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    set_source_files_properties(main.cpp PROPERTIES COMPILE_OPTIONS "-Wno-format-truncation")
endif()

enable_testing()
add_test(NAME test1 COMMAND endf_reader WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/test1)
