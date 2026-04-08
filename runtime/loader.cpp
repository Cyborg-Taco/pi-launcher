/**
 * Core loader for Minecraft Bedrock Edition on Raspberry Pi 4
 * 
 * Loads libminecraftpe.so using dlopen, resolves Android symbols,
 * and executes the game's entry point with proper environment setup.
 * 
 * Compile with: -O2 -march=armv8-a -pipe (NOT -O3)
 */

#include <iostream>
#include <string>
#include <cstring>
#include <dlfcn.h>
#include <unistd.h>
#include <sys/stat.h>
#include <errno.h>
#include <libgen.h>

// Configuration - should be set via config file or environment
struct LoaderConfig {
    std::string libPath;           // Path to libminecraftpe.so
    std::string workingDir;        // Working directory for game
    std::string shimPath;          // Path to mcpe_shim.so
    std::string assetsDir;         // Path to assets folder
    int argc;
    char** argv;
};

static LoaderConfig g_config;

// Required symbols to resolve from libminecraftpe.so
// These are the essential exports needed to bootstrap the game
static const char* g_requiredSymbols[] = {
    // Main entry point
    "main",
    
    // Android runtime hooks (provided by our shim)
    "ANativeActivity_onCreate",
    "SDL_Init",
    "SDL_CreateWindow",
    "SDL_SetVideoMode",
    "SDL_PollEvent",
    
    // Graphics (EGL provided by shim)
    "eglGetDisplay",
    "eglInitialize",
    "eglCreateWindowSurface",
    "eglCreateContext",
    "eglSwapBuffers",
    
    // Audio (provided by shim)
    "android_audio_init",
    "android_audio_play",
    
    // File I/O (provided by shim)
    "android_file_open",
    "android_file_read",
    "android_file_write",
    "android_file_close",
    
    // Memory allocation
    "malloc",
    "free",
    "realloc",
    
    nullptr // Sentinel
};

/**
 * Load configuration from environment or config file
 */
bool loadConfiguration() {
    const char* libPath = getenv("MCPE_LIB_PATH");
    const char* workingDir = getenv("MCPE_WORKING_DIR");
    const char* shimPath = getenv("MCPE_SHIM_PATH");
    const char* assetsDir = getenv("MCPE_ASSETS_DIR");
    
    // Default paths
    const char* home = getenv("HOME");
    if (!home) {
        home = "/root";
    }
    
    std::string defaultLibPath = std::string(home) + "/.local/share/mcpe-launcher/versions/default/libminecraftpe.so";
    std::string defaultShimPath = std::string(home) + "/.local/share/mcpe-launcher/shim/libmcpe_shim.so";
    std::string defaultWorkingDir = std::string(home) + "/.local/share/mcpe-launcher/versions/default";
    std::string defaultAssetsDir = std::string(home) + "/.local/share/mcpe-launcher/versions/default/assets";
    
    g_config.libPath = libPath ? libPath : defaultLibPath;
    g_config.shimPath = shimPath ? shimPath : defaultShimPath;
    g_config.workingDir = workingDir ? workingDir : defaultWorkingDir;
    g_config.assetsDir = assetsDir ? assetsDir : defaultAssetsDir;
    
    return true;
}

/**
 * Verify all required symbols are available in the loaded library
 * Crashes early with clear error if any symbol is missing
 */
bool verifySymbols(void* handle) {
    std::cout << "[Loader] Verifying required symbols..." << std::endl;
    
    for (int i = 0; g_requiredSymbols[i] != nullptr; ++i) {
        const char* symbol = g_requiredSymbols[i];
        
        // First check if symbol exists in main library
        void* addr = dlsym(handle, symbol);
        
        if (!addr) {
            // Also check in shim library
            void* shimHandle = dlopen(g_config.shimPath.c_str(), RTLD_NOW);
            if (shimHandle) {
                addr = dlsym(shimHandle, symbol);
            }
        }
        
        if (!addr) {
            std::cerr << "[Loader] FATAL: Missing required symbol: " << symbol << std::endl;
            std::cerr << "[Loader] Error: " << dlerror() << std::endl;
            return false;
        }
        
        std::cout << "  [OK] " << symbol << std::endl;
    }
    
    std::cout << "[Loader] All symbols verified successfully" << std::endl;
    return true;
}

/**
 * Setup the environment for Minecraft
 * - Set working directory
 * - Configure Mesa for VideoCore VI
 * - Setup Android path remapping
 */
void setupEnvironment() {
    // Set working directory
    if (chdir(g_config.workingDir.c_str()) != 0) {
        std::cerr << "[Loader] Warning: Failed to set working directory: " 
                  << g_config.workingDir << " (" << strerror(errno) << ")" << std::endl;
        // Try to create it
        mkdir(g_config.workingDir.c_str(), 0755);
        chdir(g_config.workingDir.c_str());
    }
    
    // Configure Mesa for VideoCore VI GPU
    // OpenGL ES 3.0 on the Pi 4's VideoCore VI
    setenv("MESA_GL_VERSION_OVERRIDE", "3.0", 1);
    setenv("MESA_EGL", "1", 1);
    
    // Disable V-sync for better performance (Pi can't handle high refresh)
    setenv("MESA_VSYNC", "0", 1);
    
    // Use software renderer as fallback
    setenv("LIBGL_ALWAYS_SOFTWARE", "1");
    
    // Android path remapping - these must match shim expectations
    setenv("MCPE_HOME", g_config.workingDir.c_str(), 1);
    setenv("ANDROID_DATA", g_config.workingDir.c_str(), 1);
    setenv("ANDROID_ROOT", "/system", 1);
    
    // Optimize for limited RAM - disable unnecessary features
    setenv("MESA_GL_USE_INVALIDATE", "1", 1);
    
    std::cout << "[Loader] Environment configured for Raspberry Pi 4 (1GB RAM)" << std::endl;
}

/**
 * Load the shim library first (provides Android stubs)
 */
void* loadShim() {
    std::cout << "[Loader] Loading shim library: " << g_config.shimPath << std::endl;
    
    // Load shim with RTLD_NOW to catch missing symbols early
    void* shimHandle = dlopen(g_config.shimPath.c_str(), RTLD_NOW | RTLD_GLOBAL);
    
    if (!shimHandle) {
        std::cerr << "[Loader] FATAL: Failed to load shim: " << dlerror() << std::endl;
        return nullptr;
    }
    
    std::cout << "[Loader] Shim loaded successfully" << std::endl;
    return shimHandle;
}

/**
 * Load and execute Minecraft
 */
int loadAndRunMinecraft() {
    // Load shim first (provides Android stubs)
    void* shimHandle = loadShim();
    if (!shimHandle) {
        std::cerr << "[Loader] FATAL: Cannot continue without shim" << std::endl;
        return 1;
    }
    
    std::cout << "[Loader] Loading Minecraft: " << g_config.libPath << std::endl;
    
    // RTLD_NOW to resolve all symbols immediately (crash early if missing)
    void* gameHandle = dlopen(g_config.libPath.c_str(), RTLD_NOW);
    
    if (!gameHandle) {
        std::cerr << "[Loader] FATAL: Failed to load libminecraftpe.so: " << dlerror() << std::endl;
        return 1;
    }
    
    std::cout << "[Loader] Game library loaded, verifying symbols..." << std::endl;
    
    // Verify all required symbols exist
    if (!verifySymbols(gameHandle)) {
        std::cerr << "[Loader] FATAL: Symbol verification failed" << std::endl;
        dlclose(gameHandle);
        return 1;
    }
    
    // Get the main entry point
    // Note: Actual symbol name depends on the Minecraft version
    // Common names: main, android_main, entry
    typedef int (*MainFunc)(int, char**);
    
    MainFunc mainFunc = (MainFunc)dlsym(gameHandle, "main");
    
    if (!mainFunc) {
        // Try alternative names
        mainFunc = (MainFunc)dlsym(gameHandle, "android_main");
    }
    
    if (!mainFunc) {
        mainFunc = (MainFunc)dlsym(gameHandle, "entry");
    }
    
    if (!mainFunc) {
        std::cerr << "[Loader] FATAL: Could not find game entry point (main/android_main/entry)" << std::endl;
        dlclose(gameHandle);
        return 1;
    }
    
    std::cout << "[Loader] Launching Minecraft..." << std::endl;
    
    // Run the game
    // Passing minimal argc/argv - the game will use its own initialization
    int result = mainFunc(g_config.argc, g_config.argv);
    
    std::cout << "[Loader] Game exited with code: " << result << std::endl;
    
    // Cleanup
    dlclose(gameHandle);
    dlclose(shimHandle);
    
    return result;
}

void printUsage(const char* progName) {
    std::cout << "Usage: " << progName << " [options]" << std::endl;
    std::cout << "Options:" << std::endl;
    std::cout << "  --lib <path>    Path to libminecraftpe.so" << std::endl;
    std::cout << "  --workdir <dir> Working directory for game" << std::endl;
    std::cout << "  --shim <path>   Path to mcpe_shim.so" << std::endl;
    std::cout << "  --version       Show version" << std::endl;
    std::cout << "  --help          Show this help" << std::endl;
}

int main(int argc, char** argv) {
    std::cout << "========================================" << std::endl;
    std::cout << "  Pi Minecraft Loader v1.0.0" << std::endl;
    std::cout << "  For Raspberry Pi 4 (arm64, 1GB RAM)" << std::endl;
    std::cout << "========================================" << std::endl;
    
    // Parse command line arguments
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        
        if (arg == "--help" || arg == "-h") {
            printUsage(argv[0]);
            return 0;
        }
        else if (arg == "--version" || arg == "-v") {
            std::cout << "pi-loader v1.0.0" << std::endl;
            return 0;
        }
        else if (arg == "--lib" && i + 1 < argc) {
            setenv("MCPE_LIB_PATH", argv[++i], 1);
        }
        else if (arg == "--workdir" && i + 1 < argc) {
            setenv("MCPE_WORKING_DIR", argv[++i], 1);
        }
        else if (arg == "--shim" && i + 1 < argc) {
            setenv("MCPE_SHIM_PATH", argv[++i], 1);
        }
    }
    
    // Load configuration
    if (!loadConfiguration()) {
        std::cerr << "[Loader] FATAL: Failed to load configuration" << std::endl;
        return 1;
    }
    
    std::cout << "[Loader] Configuration:" << std::endl;
    std::cout << "  lib:     " << g_config.libPath << std::endl;
    std::cout << "  shim:    " << g_config.shimPath << std::endl;
    std::cout << "  workdir: " << g_config.workingDir << std::endl;
    std::cout << "  assets:  " << g_config.assetsDir << std::endl;
    
    // Verify library exists
    if (access(g_config.libPath.c_str(), R_OK) != 0) {
        std::cerr << "[Loader] FATAL: Cannot access libminecraftpe.so: " 
                  << g_config.libPath << std::endl;
        std::cerr << "[Loader] Use --lib to specify correct path or download a version first" << std::endl;
        return 1;
    }
    
    // Verify shim exists
    if (access(g_config.shimPath.c_str(), R_OK) != 0) {
        std::cerr << "[Loader] FATAL: Cannot access mcpe_shim.so: " 
                  << g_config.shimPath << std::endl;
        std::cerr << "[Loader] Ensure shim is installed to: " << g_config.shimPath << std::endl;
        return 1;
    }
    
    // Setup environment
    setupEnvironment();
    
    // Store argc/argv for the game
    g_config.argc = argc;
    g_config.argv = argv;
    
    // Load and run
    return loadAndRunMinecraft();
}