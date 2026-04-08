cmake_minimum_required(VERSION 3.16)
project(pi-launcher CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

option(DEBUG_SYMBOLS "Enable debugging symbols without -O2" OFF)

if(DEBUG_SYMBOLS)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -g -march=armv8-a")
else()
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -O2 -march=armv8-a -pipe")
endif()

# We need these system libraries locally on Pi to link against
find_package(PkgConfig REQUIRED)
pkg_check_modules(SDL2 REQUIRED sdl2)

# Build the Android compatibility shim
add_library(mcpe_shim SHARED
    shim/android/libandroid_stub.cpp
    shim/egl/egl_stub.cpp
    shim/audio/audio_pulse.cpp
    shim/fs_bridge.cpp
)
# Ensure we link to PulseAudio, dl for RTLD operations, and Mesa EGL
target_link_libraries(mcpe_shim PRIVATE dl pulse EGL)

# Build the Main binary
add_executable(pi-launcher
    launcher/ui.cpp
    launcher/downloader.cpp
    runtime/loader.cpp
)

target_include_directories(pi-launcher PRIVATE ${SDL2_INCLUDE_DIRS})
# Link the standard launcher deps + the shim dynamically
target_link_libraries(pi-launcher PRIVATE dl ${SDL2_LIBRARIES} pulse zip EGL)

# Standard Linux install target
install(TARGETS pi-launcher DESTINATION /usr/local/bin)
install(TARGETS mcpe_shim DESTINATION /usr/local/lib)
