cmake_minimum_required(VERSION 3.25)

project(MyCAD VERSION 0.1.2 LANGUAGES CXX)

if(NOT WIN32)
  message(FATAL_ERROR "MyCAD currently targets Windows x64.")
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

if(NOT DEFINED OCCT_ROOT)
  message(FATAL_ERROR "OCCT_ROOT must point to the conda Library directory.")
endif()

set(OCCT_INCLUDE_DIR "${OCCT_ROOT}/include/opencascade")
set(OCCT_LIBRARY_DIR "${OCCT_ROOT}/lib")

if(NOT EXISTS "${OCCT_INCLUDE_DIR}")
  message(FATAL_ERROR "OCCT include directory not found: ${OCCT_INCLUDE_DIR}")
endif()

if(NOT EXISTS "${OCCT_LIBRARY_DIR}")
  message(FATAL_ERROR "OCCT library directory not found: ${OCCT_LIBRARY_DIR}")
endif()

# Use Qt from the same conda environment as OCCT.
list(PREPEND CMAKE_PREFIX_PATH "${OCCT_ROOT}")
find_package(Qt6 CONFIG REQUIRED COMPONENTS Widgets)

# Do not use OpenCASCADEConfig.cmake here.
# The conda package exports TKIVtk, whose imported target references VTK targets
# even though MyCAD does not use VTK. Link OCCT import libraries directly instead.
file(GLOB OCCT_TOOLKIT_LIBS CONFIGURE_DEPENDS "${OCCT_LIBRARY_DIR}/TK*.lib")
list(FILTER OCCT_TOOLKIT_LIBS EXCLUDE REGEX "TKIVtk.*\\.lib$")

if(NOT OCCT_TOOLKIT_LIBS)
  message(FATAL_ERROR "No OpenCASCADE toolkit libraries found in ${OCCT_LIBRARY_DIR}")
endif()

message(STATUS "OCCT root: ${OCCT_ROOT}")
message(STATUS "OCCT include: ${OCCT_INCLUDE_DIR}")
message(STATUS "OCCT library count: ${OCCT_TOOLKIT_LIBS}")

add_executable(MyCAD WIN32
  src/main.cpp
)

target_include_directories(MyCAD PRIVATE
  "${OCCT_INCLUDE_DIR}"
)

target_link_libraries(MyCAD PRIVATE
  Qt6::Widgets
  ${OCCT_TOOLKIT_LIBS}
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
