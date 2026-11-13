cmake_minimum_required(VERSION 3.16)
project(Forge VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets OpenGL OpenGLWidgets)

file(GLOB_RECURSE FORGE_SOURCES CONFIGURE_DEPENDS src/*.cpp src/*.h)

if(WIN32)
    add_executable(forge WIN32 ${FORGE_SOURCES})
else()
    add_executable(forge ${FORGE_SOURCES})
endif()

target_include_directories(forge PRIVATE src)
target_link_libraries(forge PRIVATE Qt6::Core Qt6::Gui Qt6::Widgets Qt6::OpenGL Qt6::OpenGLWidgets)
target_compile_definitions(forge PRIVATE FORGE_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")

if(WIN32)
    target_sources(forge PRIVATE ./resource/app.rc)
endif()


add_custom_command(TARGET forge POST_BUILD COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_CURRENT_SOURCE_DIR}/entities $<TARGET_FILE_DIR:forge>/entities)