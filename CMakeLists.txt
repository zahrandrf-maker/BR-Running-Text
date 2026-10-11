cmake_minimum_required(VERSION 3.21)
project(BRRunningText VERSION 0.1.6 LANGUAGES CXX)

if(NOT WIN32)
  message(FATAL_ERROR "BR Running Text currently supports Windows only")
endif()
include(FetchContent)
# Resolve GLEW in the top-level directory. Imported CMake targets created only
# inside FFGL's subdirectory are not visible back in this parent directory.
find_package(GLEW REQUIRED)
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
target_compile_definitions(BRRunningText PRIVATE NOMINMAX)
# BRRunningText.cpp includes FFGLSDK.h, whose public FFGL headers include
# GL/glew.h. Link GLEW directly so its include directory is available while
# compiling this target as well as while linking the SDK.
target_link_libraries(BRRunningText PRIVATE ffgl::sdk GLEW::GLEW gdiplus ole32 opengl32)
target_include_directories(BRRunningText PRIVATE ${ffgl_SOURCE_DIR}/source/lib)

set_target_properties(BRRunningText PROPERTIES
  OUTPUT_NAME "BR Running Text"
  PREFIX ""
  SUFFIX ".dll"
  RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/plugin"
  RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_BINARY_DIR}/plugin"
)

if(MSVC)
  # FFGL temporarily uses reduced Windows headers. Force the complete GDI
  # declarations for this plug-in because GDI+ needs metafile constants.
  target_compile_options(BRRunningText PRIVATE
    /W4 /permissive- /utf-8
    /UWIN32_LEAN_AND_MEAN /UNOMETAFILE /UNOGDI
  )
  target_link_options(BRRunningText PRIVATE "/DEF:${CMAKE_CURRENT_SOURCE_DIR}/FFGLPlugin.def")
endif()

