cmake_minimum_required(VERSION 3.15)
project(spdtest VERSION 1.0.0 LANGUAGES C)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)

find_package(PkgConfig REQUIRED)

# Find libcurl
find_package(CURL REQUIRED)
if(CURL_FOUND)
    message(STATUS "Found libcurl: ${CURL_VERSION_STRING}")
else()
    message(FATAL_ERROR "libcurl not found")
endif()

# Find libuv
find_library(UV_LIBRARY NAMES uv libuv PATHS /usr/local/lib /opt/homebrew/lib)
find_path(UV_INCLUDE_DIR uv.h PATHS /usr/local/include /opt/homebrew/include)

if(UV_LIBRARY AND UV_INCLUDE_DIR)
    message(STATUS "Found libuv: ${UV_LIBRARY}")
else()
    message(FATAL_ERROR "libuv not found. Install with: brew install libuv")
endif()

# Create executable
add_executable(spdtest main.c)

# Link libraries
target_link_libraries(spdtest
    ${CURL_LIBRARIES}
    ${UV_LIBRARY}
)

# Include directories
target_include_directories(spdtest PRIVATE
    ${CURL_INCLUDE_DIRS}
    ${UV_INCLUDE_DIR}
)

# Set output directory
set_target_properties(spdtest PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
)