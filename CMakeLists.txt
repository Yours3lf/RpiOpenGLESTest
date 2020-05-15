cmake_minimum_required (VERSION 3.0)

project (ogles-without-x)

add_definitions(-Wall -std=c99)

SET(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
SET(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
SET(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)

set (EXTERNAL_PATH ${CMAKE_CURRENT_SOURCE_DIR}/external/) #/home/yours3lf/Documents/oglestest/external/
link_directories(${EXTERNAL_PATH}/lib/)

FILE(GLOB SRC_FILES *.c)

add_executable(ogles-without-x ${SRC_FILES})

target_include_directories(ogles-without-x PUBLIC ${EXTERNAL_PATH}/include)

target_link_libraries(ogles-without-x EGL GLESv2
	X11 xcb xcb-dri2 xcb-dri3 xcb-sync xcb-present xcb-xfixes xshmfence Xau X11-xcb Xdmcp expat bsd
	gbm drm
	dl
	)
