cmake_minimum_required(VERSION 3.25)

project(MyCAD VERSION 0.1.0 LANGUAGES CXX)

if(NOT WIN32)
  message(FATAL_ERROR "MyCAD V0.1 currently targets Windows x64.")
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

find_package(Qt6 CONFIG REQUIRED COMPONENTS Widgets)
find_package(OpenCASCADE CONFIG REQUIRED)

message(STATUS "OpenCASCADE ${OpenCASCADE_MAJOR_VERSION}.${OpenCASCADE_MINOR_VERSION}.${OpenCASCADE_MAINTENANCE_VERSION}")
message(STATUS "OpenCASCADE include: ${OpenCASCADE_INCLUDE_DIR}")
message(STATUS "OpenCASCADE library: ${OpenCASCADE_LIBRARY_DIR}")

add_executable(MyCAD WIN32
  src/main.cpp
)

target_include_directories(MyCAD PRIVATE
  ${OpenCASCADE_INCLUDE_DIR}
)

target_link_directories(MyCAD PRIVATE
  ${OpenCASCADE_LIBRARY_DIR}
)

target_link_libraries(MyCAD PRIVATE
  Qt6::Widgets
  ${OpenCASCADE_LIBRARIES}
)

target_compile_definitions(MyCAD PRIVATE
  NOMINMAX
  WIN32_LEAN_AND_MEAN
)

if(MSVC)
  target_compile_options(MyCAD PRIVATE /utf-8 /EHsc /W3)
endif()

set_target_properties(MyCAD PROPERTIES
  OUTPUT_NAME "MyCAD"
  VS_DEBUGGER_WORKING_DIRECTORY "$<TARGET_FILE_DIR:MyCAD>"
)
