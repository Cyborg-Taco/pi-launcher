# Minecraft Bedrock Pi Launcher

A custom launcher and compatibility layer to run the official Android arm64 Minecraft Bedrock Edition binary natively on a Raspberry Pi 4 (1GB RAM) with maximum CPU and RAM efficiency.

## Features
- **Core Loader:** Loads `libminecraftpe.so` resolving dependencies directly natively.
- **Android Compatibility Shim:** Intercepts JNI, Android inputs, and sensors with null-stubs. 
- **VideoCore VI EGL Bridge:** Forces `MESA_GL_VERSION_OVERRIDE=3.0` directly mapped to standard ES contexts.
- **PulseAudio Bridge:** Bypasses `AudioTrack` API cleanly linking chunked, minimal-wakeup buffers directly to PulseAudio.
- **Filesystem Remapper:** Re-routes `/data/...` and `/sdcard/...` Android file queries logically into your home directory (`~/.local/share/mcpe-launcher/...`).
- **Memory Efficient Engine:** Under 20MB launcher memory footprint using pure SDL2 software rendering. Zero GUI fork overlap—the UI footprint fully collapses (`execl`) prior to executing the game environment to save critical RAM.

## System Requirements
- **Device:** Raspberry Pi 4 (Optimized for 1GB RAM constraints)
- **OS:** Raspberry Pi OS 64-bit (Debian Bookworm)

## Build Prerequisites
You will need the standard C++ toolchain and development headers for the cross-platform dependencies.
Run the following before attempting to build:

```bash
sudo apt update
sudo apt install build-essential cmake pkg-config libsdl2-dev libzip-dev libpulse-dev libegl1-mesa-dev curl
```

## Compilation & Installation

The project uses CMake and requires C++17 support.

```bash
# Provide a standard out-of-source CMake build
mkdir build
cd build

# Configure the Makefile system targeting native Pi 64-bit dependencies
cmake ..

# Optional: To include debug symbols without compile optimization, run:
# cmake -DDEBUG_SYMBOLS=ON ..

# Build the main runner and library shim (.so)
make -j4

# Install `/usr/local/bin/pi-launcher` and `/usr/local/lib/mcpe_shim.so`
sudo make install
```

## How to play
To open the graphical launcher:
```bash
pi-launcher
```
1. Click the green boundary on the left to enter an active APK URL in the parent console terminal. The software will download and strip out `.so` dependencies alongside the necessary static assets.
2. Click the right boundary to execute the engine. The UI terminates cleanly to reserve memory and passes execution securely.
