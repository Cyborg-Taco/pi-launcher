/**
 * Filesystem Bridge - remap Android paths to Linux paths
 * 
 * Remaps Android-specific paths to Linux equivalents:
 * - /data/data/com.mojang.minecraftpe -> ~/.local/share/mcpe-launcher
 * - /sdcard/games/com.mojang -> ~/.local/share/mcpe-launcher
 * 
 * Uses environment variables set by loader for configuration.
 */

#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <fcntl.h>
#include <cstring>
#include <cerrno>
#include <iostream>
#include <map>
#include <string>

// ============================================================================
// Path Mappings
// ============================================================================

// Android path prefix -> Linux path prefix
static std::map<std::string, std::string> g_pathMappings;

// ============================================================================
// Initialization
// ============================================================================

/**
 * Initialize path mappings based on environment
 */
static void initPathMappings() {
    const char* home = getenv("HOME");
    if (!home) home = "/root";
    
    const char* mcpeHome = getenv("MCPE_HOME");
    if (!mcpeHome) {
        mcpeHome = (std::string(home) + "/.local/share/mcpe-launcher").c_str();
    }
    
    // Define path remappings
    g_pathMappings["/data/data/com.mojang.minecraftpe"] = mcpeHome;
    g_pathMappings["/sdcard/games/com.mojang"] = mcpeHome;
    g_pathMappings["/sdcard/games/com.mojang.minecraftpe"] = mcpeHome;
    g_pathMappings["/storage/emulated/0/games/com.mojang.minecraftpe"] = mcpeHome;
    g_pathMappings["/storage/emulated/0/Android/data/com.mojang.minecraftpe/files"] = mcpeHome;
    
    // Log mappings
    std::cout << "[Filesystem] Path mappings:" << std::endl;
    for (const auto& pair : g_pathMappings) {
        std::cout << "  " << pair.first << " -> " << pair.second << std::endl;
    }
}

/**
 * Remap an Android path to Linux path
 */
static std::string remapPath(const char* path) {
    if (!path) return "";
    
    std::string result = path;
    
    // Check each mapping
    for (const auto& pair : g_pathMappings) {
        if (result.find(pair.first) == 0) {
            // Replace the prefix
            result = pair.second + result.substr(pair.first.length());
            break;
        }
    }
    
    return result;
}

// ============================================================================
// File Operations (Android-compatible names)
// ============================================================================

extern "C" {

/**
 * Open a file (Android-style)
 * 
 * mode: O_RDONLY, O_WRONLY, O_RDWR, O_CREAT, etc.
 * Returns: file descriptor (negative on error)
 * 
 * Note: This is a minimal implementation. A full implementation would
 * also handle Android-specific file operations and permissions.
 */
int android_file_open(const char* path, int mode) {
    if (!path) {
        errno = EINVAL;
        return -1;
    }
    
    // Remap Android path to Linux path
    std::string linuxPath = remapPath(path);
    
    // Ensure parent directory exists
    size_t lastSlash = linuxPath.rfind('/');
    if (lastSlash != std::string::npos) {
        std::string parent = linuxPath.substr(0, lastSlash);
        mkdir(parent.c_str(), 0755);
    }
    
    // Open the file
    int fd = open(linuxPath.c_str(), mode, 0644);
    
    if (fd < 0) {
        std::cerr << "[Filesystem] Failed to open " << linuxPath << ": " << strerror(errno) << std::endl;
        return fd;
    }
    
    std::cout << "[Filesystem] Opened: " << linuxPath << " (fd=" << fd << ")" << std::endl;
    return fd;
}

/**
 * Read from file descriptor
 */
int android_file_read(int fd, void* buf, int count) {
    if (fd < 0 || !buf || count <= 0) {
        errno = EINVAL;
        return -1;
    }
    
    return read(fd, buf, count);
}

/**
 * Write to file descriptor
 */
int android_file_write(int fd, const void* buf, int count) {
    if (fd < 0 || !buf || count <= 0) {
        errno = EINVAL;
        return -1;
    }
    
    return write(fd, buf, count);
}

/**
 * Close file descriptor
 */
int android_file_close(int fd) {
    if (fd < 0) {
        errno = EINVAL;
        return -1;
    }
    
    return close(fd);
}

/**
 * Seek file position
 */
off_t android_file_seek(int fd, off_t offset, int whence) {
    if (fd < 0) {
        errno = EINVAL;
        return -1;
    }
    
    return lseek(fd, offset, whence);
}

/**
 * Get file status
 */
int android_file_stat(const char* path, struct stat* st) {
    if (!path || !st) {
        errno = EINVAL;
        return -1;
    }
    
    std::string linuxPath = remapPath(path);
    return stat(linuxPath.c_str(), st);
}

/**
 * Check if file exists
 */
bool android_file_exists(const char* path) {
    if (!path) return false;
    
    std::string linuxPath = remapPath(path);
    struct stat st;
    return stat(linuxPath.c_str(), &st) == 0;
}

/**
 * Create directory
 */
int android_mkdir(const char* path, mode_t mode) {
    if (!path) {
        errno = EINVAL;
        return -1;
    }
    
    std::string linuxPath = remapPath(path);
    return mkdir(linuxPath.c_str(), mode);
}

/**
 * Remove file
 */
int android_remove(const char* path) {
    if (!path) {
        errno = EINVAL;
        return -1;
    }
    
    std::string linuxPath = remapPath(path);
    return remove(linuxPath.c_str());
}

/**
 * Rename/move file
 */
int android_rename(const char* oldPath, const char* newPath) {
    if (!oldPath || !newPath) {
        errno = EINVAL;
        return -1;
    }
    
    std::string linuxOldPath = remapPath(oldPath);
    std::string linuxNewPath = remapPath(newPath);
    return rename(linuxOldPath.c_str(), linuxNewPath.c_str());
}

/**
 * Get file size
 */
off_t android_file_size(int fd) {
    if (fd < 0) {
        errno = EINVAL;
        return -1;
    }
    
    struct stat st;
    if (fstat(fd, &st) < 0) {
        return -1;
    }
    
    return st.st_size;
}

} // extern "C"

// Static initialization
static __attribute__((constructor)) void filesystem_bridge_init() {
    initPathMappings();
    std::cout << "[Filesystem] Filesystem bridge initialized" << std::endl;
}