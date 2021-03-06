# 
# Arane's CMakeLists file.
# 

CMAKE_MINIMUM_REQUIRED(VERSION 2.8)
PROJECT(Arane)

FILE(GLOB_RECURSE Arane_Headers ${CMAKE_SOURCE_DIR}/include/*.hpp)
FILE(GLOB_RECURSE Arane_SOURCES ${CMAKE_SOURCE_DIR}/src/*.cpp)

ADD_SUBDIRECTORY(cmake)
SET(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} "${CMAKE_SOURCE_DIR}/cmake/Modules/")
SET(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/build/)
SET(LIB_SUFFIX "64")
INCLUDE_DIRECTORIES(include/)
ADD_EXECUTABLE(arane ${Arane_SOURCES} ${Arane_Headers})

# 
# Dependencies.
#
#-------------------------------------------------------------------------------

# GMP
FIND_PACKAGE(GMP)
INCLUDE_DIRECTORIES(${GMP_INCLUDE_DIR})

#-------------------------------------------------------------------------------

TARGET_LINK_LIBRARIES(arane ${GMP_LIBRARIES})

SET(CMAKE_CXX_FLAGS "-Wall -O3 -std=c++11")

