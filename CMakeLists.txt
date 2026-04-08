cmake_minimum_required(VERSION 3.16)
project(pi-mcpe-launcher VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Default build flags for Raspberry Pi 4 (arm64)
set(DEFAULT_CFLAGS "-O2 -march=armv8-a -pipe")
set(DEFAULT_LDFLAGS "-latomic")

# Parse arguments
option(DEBUG_SYMBOLS "Enable debug symbols (-g without -O2)" OFF)

if(DEBUG_SYMBOLS)
    set(CMAKE_CXX_FLAGS "${DEFAULT_CFLAGS} -g")
else()
    set(CMAKE_CXX_FLAGS "${DEFAULT_CFLAGS}")
endif()

# Set compiler for aarch64
set(CMAKE_C_COMPILER aarch64-linux-gnu-gcc)
set(CMAKE_CXX_COMPILER aarch64-linux-gnu-g++)

# Find required libraries
find_package(PkgConfig REQUIRED)
pkg_check_modules(SDL2 REQUIRED SDL2)
pkg_check_modules(libpulse libpulse-simple)
pkg_check_modules(libzip REQUIRED libzip)
pkg_check_modules(libEGL REQUIRED EGL)

# Configuration
set(VERSION_INSTALL_DIR "$ENV{HOME}/.local/share/mcpe-launcher/versions" CACHE PATH "Where to install game versions")
set(SHIM_INSTALL_DIR "$ENV{HOME}/.local/share/mcpe-launcher/shim" CACHE PATH "Shim library location")

# Create output directories
file(MAKE_DIRECTORY "${VERSION_INSTALL_DIR}")
file(MAKE_DIRECTORY "${SHIM_INSTALL_DIR}")

# =============================================================================
# SHIM LIBRARY (shared library for Android compatibility)
# =============================================================================
add_library(mcpe_shim SHARED
    shim/android/activity_stub.cpp
    shim/android/jni_stub.cpp
    shim/egl/egl_bridge.cpp
    shim/audio/pulse_bridge.cpp
    shim/filesystem_bridge.cpp
)

target_include_directories(mcpe_shim PUBLIC
    ${SDL2_INCLUDE_DIRS}
    ${libpulse_INCLUDE_DIRS}
    ${libEGL_INCLUDE_DIRS}
)

target_link_libraries(mcpe_shim PRIVATE
    ${SDL2_LIBRARIES}
    ${libpulse_LIBRARIES}
    ${libEGL_LIBRARIES}
    dl
    atomic
)

target_compile_options(mcpe_shim PRIVATE -fPIC -shared)
set_target_properties(mcpe_shim PROPERTIES OUTPUT_NAME "mcpe_shim")

# Install shim
install(TARGETS mcpe_shim LIBRARY DESTINATION "${SHIM_INSTALL_DIR}")

# =============================================================================
# RUNTIME LOADER
# =============================================================================
add_executable(pi-loader runtime/loader.cpp)

target_link_libraries(pi-loader PRIVATE dl)

target_compile_options(pi-loader PRIVATE
    $<$<CONFIG:Debug>:-g>
)

install(TARGETS pi-loader RUNTIME DESTINATION bin)

# =============================================================================
# VERSION DOWNLOADER
# =============================================================================
add_executable(pi-downloader launcher/downloader.cpp)

target_link_libraries(pi-downloader PRIVATE ${libzip_LIBRARIES})

install(TARGETS pi-downloader RUNTIME DESTINATION bin)

# =============================================================================
# SDL2 LAUNCHER UI
# =============================================================================
add_executable(pi-launcher launcher/ui.cpp)

target_include_directories(pi-launcher PRIVATE ${SDL2_INCLUDE_DIRS})

target_link_libraries(pi-launcher PRIVATE
    ${SDL2_LIBRARIES}
    dl
)

# Full cleanup before exec - ensure no forking memory leaks
set_target_properties(pi-launcher PROPERTIES
    OUTPUT_NAME "pi-launcher"
)

install(TARGETS pi-launcher RUNTIME DESTINATION bin)

# =============================================================================
# MAIN INSTALL TARGET
# =============================================================================
install(TARGETS pi-loader pi-downloader pi-launcher LIBRARY DESTINATION "${SHIM_INSTALL_DIR}" COMPONENT shim)