#
# See
# https://cmake.org/cmake/help/v3.0/manual/cmake-commands.7.html
# http://www.slideshare.net/DanielPfeifer1/cmake-48475415
#
CMAKE_MINIMUM_REQUIRED(VERSION 2.8 FATAL_ERROR)

# Project(<name> VERSION <version> LANGUAGES CXX)
PROJECT(AIO C CXX ASM)

set(CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/CMake" "${CMAKE_MODULE_PATH}")

# so that "make" will echo the command line for easy debugging
SET(CMAKE_VERBOSE_MAKEFILE on)

# Specify command-line options
# OPTION(<option_variable> "help string describing option" [initial_value])

# what is build type; override on command line with -D
#SET (CMAKE_BUILD_TYPE Release)
# CMAKE_BUILD_TYPE can be Release/Debug/RelWithDebInfo/MinSizeRel
if (NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  message(STATUS "No build type selected, default to Debug")
  set(CMAKE_BUILD_TYPE "Debug")
endif()

# turns on colors in generated Makefile
SET(CMAKE_COLOR_MAKEFILE)

# overwriting the source and binary directories with the current ones
# this is useful for other projects reusing this project as a subcomponent
SET(CMAKE_SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR})
SET(CMAKE_BINARY_DIR ${CMAKE_CURRENT_BINARY_DIR})

# this will prohibit in-source-builds
IF(${CMAKE_BINARY_DIR} STREQUAL ${CMAKE_SOURCE_DIR})
  MESSAGE(STATUS "In-source-builds are not allowed")
  MESSAGE(STATUS "Clean your source directory (e.g. delete the CMakeCache.txt file)")
  MESSAGE(FATAL_ERROR "Please create a separate build directory and call CMake again")
ENDIF(${CMAKE_BINARY_DIR} STREQUAL ${CMAKE_SOURCE_DIR})

# TODO Generate a config file so we can embed version string in executable
SET (OFFLOADER_VERSION_MAJOR 1)
SET (OFFLOADER_VERSION_MINOR 0)
SET (OFFLOADER_PATCH_NUMBER 0)

# set compiler to gcc
SET(CMAKE_CXX_COMPILER "/usr/bin/g++")
SET(CMAKE_C_COMPILER "/usr/bin/gcc")

SET (CXX_STANDARD "c++11")
SET (CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=${CXX_STANDARD}")

INCLUDE_DIRECTORIES(/usr/local/include)

ADD_COMPILE_OPTIONS(
  -Wall
  -Wextra
  -Wstrict-aliasing
  -Wno-unused-parameter
  -Wno-missing-field-initializers
  -Wnon-virtual-dtor
  -Wchar-subscripts
  -Wpointer-arith
  -Woverloaded-virtual
  -Wformat
  -Wformat-security
  -Werror=format-security
  -fstack-protector-all
  -fPIE
  -fpie
  -fPIC
  -fpic
  -pipe
  -fdata-sections
  -ffunction-sections
)

SET(SOURCES
  ioexec.cpp
  AsyncIO.cpp
  )

ADD_EXECUTABLE(ioexec ${SOURCES})

TARGET_LINK_LIBRARIES(ioexec 
  wangle
  folly
  boost_system
  tcmalloc
  glog gflags unwind
  aio
  bz2 pthread rt z
  )
