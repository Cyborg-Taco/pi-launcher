#include <iostream>
#include <string>
#include <dlfcn.h>
#include <unistd.h>
#include <cstdlib>

// The core runtime loader. Responsibilities:
// 1. Resolve Android symbols from the dynamic lib via dlopen with RTLD_NOW.
// 2. Set up working directory.
// 3. Initiate the application main point.

int main_run_game(const std::string& so_path, int argc, char** argv) {
    std::cout << "[Loader] Setting up working directory." << std::endl;
    std::string home = getenv("HOME") ? getenv("HOME") : "/home/pi";
    std::string work_dir = home + "/.local/share/mcpe-launcher";
    chdir(work_dir.c_str());

    std::cout << "[Loader] Attempting dlopen resolving statically missing symbols: " << so_path << std::endl;
    // Loading with RTLD_NOW enforces resolution. If symbols are missing in our library shim or native libs,
    // this will crash cleanly here early instead of triggering a SIGSEGV during gameplay.
    void* handle = dlopen(so_path.c_str(), RTLD_NOW | RTLD_GLOBAL);
    if (!handle) {
        std::cerr << "[Loader] FATAL: Failed to resolve all game symbols!\nError: " << dlerror() << std::endl;
        exit(1);
    }
    
    // Most Android applications export an 'android_main' entry. Some direct port versions use standard 'main'.
    typedef int (*main_t)(int, char**);
    main_t game_main = (main_t)dlsym(handle, "main");
    if (!game_main) {
        typedef void (*android_main_t)(void*); // Normally struct android_app*
        android_main_t android_main = (android_main_t)dlsym(handle, "android_main");
        if (android_main) {
             std::cout << "[Loader] Executing android_main..." << std::endl;
             android_main(nullptr); // Handled by shim intercept
             exit(0);
        }
        std::cerr << "[Loader] Fatal: Found neither 'main' nor 'android_main' entry point in " << so_path << std::endl;
        exit(1);
    }

    std::cout << "[Loader] Calling 'main' entry point..." << std::endl;
    return game_main(argc, argv);
}
