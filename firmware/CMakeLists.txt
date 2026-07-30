cmake_minimum_required(VERSION 3.20)

set(EXTRA_COMPONENT_DIRS components)

# Strip -dirty from git describe for cleaner version display
execute_process(COMMAND git describe --tags --always
    WORKING_DIRECTORY ${CMAKE_CURRENT_LIST_DIR}
    OUTPUT_VARIABLE _git_desc
    OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
if(_git_desc)
    string(REGEX REPLACE "-dirty$" "" PROJECT_VER "${_git_desc}")
    set(PROJECT_VER "${PROJECT_VER}" CACHE STRING "Project version")
endif()

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(NukCPGDrop)
