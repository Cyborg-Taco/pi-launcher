#include <string>
#include <dlfcn.h>
#include <unistd.h>
#include <cstdlib>
#include <cstdio>
#include <cstdarg>
#include <fcntl.h>
#include <sys/types.h>

// Remap Android data requests onto the safe local folder infrastructure

static std::string remap_android_path(const char* path) {
    if (!path) return "";
    std::string s(path);
    std::string home = getenv("HOME") ? getenv("HOME") : "/home/pi";
    std::string base = home + "/.local/share/mcpe-launcher/";
    if (s.find("/data/data/com.mojang.minecraftpe") == 0) {
        return base + "data" + s.substr(33);
    }
    if (s.find("/sdcard/games/com.mojang") == 0) {
        return base + "sdcard" + s.substr(24);
    }
    return s;
}

extern "C" {
    FILE* fopen(const char* pathname, const char* mode) {
        typedef FILE* (*orig_fopen_t)(const char*, const char*);
        static orig_fopen_t orig_fopen = (orig_fopen_t)dlsym(RTLD_NEXT, "fopen");
        return orig_fopen(remap_android_path(pathname).c_str(), mode);
    }

    int open(const char* pathname, int flags, ...) {
        typedef int (*orig_open_t)(const char*, int, ...);
        static orig_open_t orig_open = (orig_open_t)dlsym(RTLD_NEXT, "open");
        mode_t mode = 0;
        if (flags & O_CREAT) {
            va_list args;
            va_start(args, flags);
            mode = va_arg(args, int); // Fetch mode reliably
            va_end(args);
        }
        return orig_open(remap_android_path(pathname).c_str(), flags, mode);
    }
}
