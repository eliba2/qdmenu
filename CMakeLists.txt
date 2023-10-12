cmake_minimum_required(VERSION 3.16)

project(qdmenu VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_INCLUDE_CURRENT_DIR YES)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Qt6 REQUIRED COMPONENTS Widgets)
qt_standard_project_setup()

qt_add_executable(qdmenu
    src/qdmenu.cpp
    src/drw.cpp
    src/util.cpp
)

set( qdmenu_HEADER
    .
)

target_link_libraries(qdmenu PRIVATE Qt6::Widgets)

set_target_properties(qdmenu PROPERTIES
    WIN32_EXECUTABLE ON
    MACOSX_BUNDLE OFF
    AUTOMOC TRUE
)

set(CMAKE_AUTOMOC_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/)

# dmenu version
set(DMENU_VERSION 5.2)
add_compile_definitions(DMENU_VERSION="dmenu-${DMENU_VERSION}")
