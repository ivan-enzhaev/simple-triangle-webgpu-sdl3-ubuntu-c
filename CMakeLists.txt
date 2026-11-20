cmake_minimum_required(VERSION 3.21)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE "Debug") # "Debug" or "Release"
endif()

project(simple-triangle-webgpu-sdl3-ubuntu-c LANGUAGES C)

set(CMAKE_C_STANDARD 17)
set(CMAKE_C_STANDARD_REQUIRED ON)

# =============================================================================
# Paths (All library versions visible in one place)
# =============================================================================
set(LIBS_ROOT "$ENV{HOME}/libs")
set(SDL3_PATH "${LIBS_ROOT}/sdl3-3.4.20-ubuntu")
set(WGPU_PATH "${LIBS_ROOT}/wgpu-native-29.0.1.1-ubuntu")

# =============================================================================
# Target
# =============================================================================
set(APP_TARGET app)

set(PROJECT_SOURCES
    src/main.c
)

add_executable(${APP_TARGET} ${PROJECT_SOURCES})

target_include_directories(${APP_TARGET} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

# =============================================================================
# Packages & Imported Libraries
# =============================================================================

# --- SDL3 (Official Config -> handles dl/pthread/m/...) --------------------
find_package(SDL3 REQUIRED CONFIG
    PATHS "${SDL3_PATH}/lib/cmake/SDL3"
    NO_DEFAULT_PATH
    NO_CMAKE_FIND_ROOT_PATH
)

if(TARGET SDL3::SDL3-static)
    set(SDL3_TARGET SDL3::SDL3-static)
else()
    set(SDL3_TARGET SDL3::SDL3)
endif()

# --- wgpu-native (No CMake config -> manual IMPORTED target) -----------------
add_library(wgpu_native STATIC IMPORTED)
set_target_properties(wgpu_native PROPERTIES
    IMPORTED_LOCATION "${WGPU_PATH}/lib/libwgpu_native.a"
    INTERFACE_INCLUDE_DIRECTORIES "${WGPU_PATH}/include"
    INTERFACE_LINK_LIBRARIES "dl;pthread;m"
)

# =============================================================================
# Link Libraries, Flags & Stripping
# =============================================================================
target_link_libraries(${APP_TARGET} PRIVATE
    ${SDL3_TARGET}
    wgpu_native
)

target_link_options(${APP_TARGET} PRIVATE -static-libgcc)

if(CMAKE_BUILD_TYPE STREQUAL "Release")
    target_compile_options(${APP_TARGET} PRIVATE -Os)
    target_link_options(${APP_TARGET} PRIVATE -s)
endif()
