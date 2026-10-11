cmake_minimum_required(VERSION 3.21)
project(BRRunningText VERSION 0.1.0 LANGUAGES CXX)

if(NOT WIN32)
  message(FATAL_ERROR "BR Running Text currently supports Windows only")
endif()
include(FetchContent)
set(FFGL_BUILD_EXAMPLE_PLUGINS OFF CACHE BOOL "" FORCE)
FetchContent_Declare(
  ffgl
  GIT_REPOSITORY https://github.com/resolume/ffgl.git
  # Pinned official SDK revision so future upstream changes do not break builds.
  GIT_TAG 5b6e07840277df9ec35a228b9eb067636316350f
  GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(ffgl)

add_library(BRRunningText SHARED
  src/BRRunningText.cpp
  src/BRRunningText.h
  FFGLPlugin.def
)

target_compile_features(BRRunningText PRIVATE cxx_std_17)
target_compile_definitions(BRRunningText PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
target_link_libraries(BRRunningText PRIVATE ffgl::sdk gdiplus opengl32)
target_include_directories(BRRunningText PRIVATE ${ffgl_SOURCE_DIR}/source/lib)

set_target_properties(BRRunningText PROPERTIES
  OUTPUT_NAME "BR Running Text"
  PREFIX ""
  SUFFIX ".dll"
  RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/plugin"
  RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_BINARY_DIR}/plugin"
)

if(MSVC)
  target_compile_options(BRRunningText PRIVATE /W4 /permissive- /utf-8)
  target_link_options(BRRunningText PRIVATE "/DEF:${CMAKE_CURRENT_SOURCE_DIR}/FFGLPlugin.def")
endif()

