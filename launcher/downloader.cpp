/**
 * Version Downloader - Download and extract Minecraft APK
 * 
 * Downloads Minecraft APK from a user-provided URL, extracts the
 * arm64-v8a libraries and assets, and saves them to the versions directory.
 * 
 * Shows download progress as percentage on stdout (no GUI).
 * 
 * Dependencies: libzip (for APK extraction)
 */

#include <iostream>
#include <string>
#include <cstring>
#include <cstdlib>
#include <sys/stat.h>
#include <sys/types.h>
#include <fcntl.h>
#include <unistd.h>
#include <curl/curl.h>
#include <zip.h>

// ============================================================================
// Configuration
// ============================================================================

// Default install location
static const char* DEFAULT_VERSION_DIR = "~/.local/share/mcpe-launcher/versions";

// ============================================================================
// Progress Tracking
// ============================================================================

struct DownloadProgress {
    double totalBytes;
    double downloadedBytes;
    bool started;
    
    DownloadProgress() : totalBytes(0), downloadedBytes(0), started(false) {}
};

static DownloadProgress g_progress;

/**
 * curl progress callback - updates progress and prints percentage
 */
static int progressCallback(void* clientp, curl_off_t dltotal, curl_off_t dlnow,
                            curl_off_t ultotal, curl_off_t ulnow) {
    (void)ultotal;
    (void)ulnow;
    (void)clientp;
    
    g_progress.totalBytes = (double)dltotal;
    g_progress.downloadedBytes = (double)dlnow;
    g_progress.started = true;
    
    if (g_progress.totalBytes > 0) {
        double percent = (g_progress.downloadedBytes / g_progress.totalBytes) * 100.0;
        
        // Update progress bar
        int barWidth = 40;
        int pos = (int)(barWidth * percent / 100.0);
        
        std::cout << "\r[";
        for (int i = 0; i < barWidth; ++i) {
            if (i < pos) std::cout << "=";
            else if (i == pos) std::cout << ">";
            else std::cout << " ";
        }
        std::cout << "] " << (int)percent << "% (";
        
        // Format sizes
        if (g_progress.downloadedBytes < 1024) {
            std::cout << (int)g_progress.downloadedBytes << "B";
        } else if (g_progress.downloadedBytes < 1024 * 1024) {
            std::cout << (int)(g_progress.downloadedBytes / 1024) << "KB";
        } else {
            std::cout << (int)(g_progress.downloadedBytes / (1024 * 1024)) << "MB";
        }
        
        std::cout << " / ";
        if (g_progress.totalBytes < 1024) {
            std::cout << (int)g_progress.totalBytes << "B";
        } else if (g_progress.totalBytes < 1024 * 1024) {
            std::cout << (int)(g_progress.totalBytes / 1024) << "KB";
        } else {
            std::cout << (int)(g_progress.totalBytes / (1024 * 1024)) << "MB";
        }
        std::cout << ")" << std::flush;
    }
    
    return 0;  // Continue download
}

// ============================================================================
// File Operations
// ============================================================================

/**
 * Ensure directory exists (create if needed)
 */
static int ensureDir(const char* path) {
    if (!path) return -1;
    
    struct stat st;
    if (stat(path, &st) == 0) {
        if (S_ISDIR(st.st_mode)) return 0;
        return -1;  // Exists but is not a directory
    }
    
    return mkdir(path, 0755);
}

/**
 * Expand ~ in path to HOME
 */
static std::string expandPath(const char* path) {
    if (!path) return "";
    
    if (path[0] == '~' && path[1] == '/') {
        const char* home = getenv("HOME");
        if (home) {
            return std::string(home) + (path + 1);
        }
    }
    
    return std::string(path);
}

/**
 * Download file from URL to local path
 */
static bool downloadFile(const std::string& url, const std::string& outputPath) {
    std::cout << "[Downloader] Downloading: " << url << std::endl;
    std::cout << "[Downloader] Output: " << outputPath << std::endl;
    
    CURL* curl = curl_easy_init();
    if (!curl) {
        std::cerr << "[Downloader] FATAL: Failed to initialize curl" << std::endl;
        return false;
    }
    
    FILE* outputFile = fopen(outputPath.c_str(), "wb");
    if (!outputFile) {
        std::cerr << "[Downloader] FATAL: Cannot create output file: " << outputPath << std::endl;
        curl_easy_cleanup(curl);
        return false;
    }
    
    // Reset progress
    g_progress = DownloadProgress();
    
    // Configure curl
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, fwrite);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, outputFile);
    curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);
    curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, progressCallback);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_MAXREDIRS, 5L);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "Mozilla/5.0 (Linux; Android 12) AppleWebKit/537.36");
    
    // Perform download
    CURLcode result = curl_easy_perform(curl);
    fclose(outputFile);
    
    std::cout << std::endl;
    
    if (result != CURLE_OK) {
        std::cerr << "[Downloader] FATAL: Download failed: " << curl_easy_strerror(result) << std::endl;
        curl_easy_cleanup(curl);
        unlink(outputPath.c_str());
        return false;
    }
    
    long responseCode;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &responseCode);
    
    if (responseCode != 200) {
        std::cerr << "[Downloader] FATAL: HTTP error: " << responseCode << std::endl;
        curl_easy_cleanup(curl);
        unlink(outputPath.c_str());
        return false;
    }
    
    curl_easy_cleanup(curl);
    std::cout << "[Downloader] Download complete!" << std::endl;
    return true;
}

/**
 * Extract files from APK
 */
static bool extractFromAPK(const std::string& apkPath, const std::string& versionDir) {
    std::cout << "[Downloader] Extracting from APK: " << apkPath << std::endl;
    
    int error;
    zip_t* zip = zip_open(apkPath.c_str(), 0, &error);
    if (!zip) {
        std::cerr << "[Downloader] FATAL: Cannot open APK: error " << error << std::endl;
        return false;
    }
    
    // Get number of entries
    zip_int64_t numEntries = zip_get_num_entries(zip, 0);
    std::cout << "[Downloader] APK contains " << numEntries << " entries" << std::endl;
    
    // Track what we extract
    int extractedLibs = 0;
    int extractedAssets = 0;
    
    // Iterate through all entries
    for (zip_int64_t i = 0; i < numEntries; ++i) {
        const char* name = zip_get_name(zip, i, 0);
        if (!name) continue;
        
        std::string entryName(name);
        
        // Check if this is a lib/arm64-v8a file or asset
        bool isLib = entryName.find("lib/arm64-v8a/") == 0 && entryName.ends_with(".so");
        bool isAsset = entryName.find("assets/") == 0;
        
        if (!isLib && !isAsset) continue;
        
        // Get the file
        zip_file_t* file = zip_fopen_index(zip, i, 0);
        if (!file) continue;
        
        // Determine output path
        std::string outputPath = versionDir + "/" + entryName;
        
        // Ensure parent directory exists
        size_t lastSlash = outputPath.rfind('/');
        if (lastSlash != std::string::npos) {
            std::string parent = outputPath.substr(0, lastSlash);
            ensureDir(parent.c_str());
        }
        
        // Get uncompressed size
        zip_stat_t stat;
        zip_stat_index(zip, i, 0, &stat);
        
        // Read and write file
        if (stat.size > 0) {
            char* buffer = new char[stat.size];
            zip_int64_t bytesRead = zip_fread(file, buffer, stat.size);
            
            if (bytesRead > 0) {
                FILE* outFile = fopen(outputPath.c_str(), "wb");
                if (outFile) {
                    fwrite(buffer, 1, bytesRead, outFile);
                    fclose(outFile);
                    
                    if (isLib) {
                        extractedLibs++;
                        std::cout << "  [LIB] " << entryName.substr(entryName.rfind('/') + 1) << std::endl;
                    } else {
                        extractedAssets++;
                    }
                }
            }
            
            delete[] buffer;
        }
        
        zip_fclose(file);
    }
    
    zip_close(zip);
    
    std::cout << "[Downloader] Extracted " << extractedLibs << " libraries and " 
              << extractedAssets << " asset files" << std::endl;
    
    return extractedLibs > 0;
}

/**
 * Clean up downloaded APK (we only need the extracted files)
 */
static void cleanupAPK(const std::string& apkPath) {
    if (unlink(apkPath.c_str()) == 0) {
        std::cout << "[Downloader] Cleaned up temporary APK" << std::endl;
    }
}

// ============================================================================
// Main
// ============================================================================

static void printUsage(const char* progName) {
    std::cout << "Usage: " << progName << " [options]" << std::endl;
    std::cout << std::endl;
    std::cout << "Options:" << std::endl;
    std::cout << "  --url <url>      URL to download Minecraft APK from" << std::endl;
    std::cout << "  --version <ver>  Version name (default: 'default')" << std::endl;
    std::cout << "  --dir <path>     Install directory (default: ~/.local/share/mcpe-launcher/versions)" << std::endl;
    std::cout << "  --help           Show this help" << std::endl;
    std::cout << std::endl;
    std::cout << "Examples:" << std::endl;
    std::cout << "  " << progName << " --url https://example.com/minecraft.apk" << std::endl;
    std::cout << "  " << progName << " --url https://example.com/minecraft.apk --version 1.21" << std::endl;
}

int main(int argc, char** argv) {
    std::cout << "========================================" << std::endl;
    std::cout << "  Pi Minecraft Downloader v1.0.0" << std::endl;
    std::cout << "  For Raspberry Pi 4 (arm64)" << std::endl;
    std::cout << "========================================" << std::endl;
    
    // Parse arguments
    std::string apkUrl;
    std::string versionName = "default";
    std::string versionDir = expandPath(DEFAULT_VERSION_DIR);
    
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        
        if (arg == "--help" || arg == "-h") {
            printUsage(argv[0]);
            return 0;
        }
        else if (arg == "--url" && i + 1 < argc) {
            apkUrl = argv[++i];
        }
        else if (arg == "--version" && i + 1 < argc) {
            versionName = argv[++i];
        }
        else if (arg == "--dir" && i + 1 < argc) {
            versionDir = expandPath(argv[++i]);
        }
    }
    
    // Validate arguments
    if (apkUrl.empty()) {
        std::cerr << "[Downloader] FATAL: No URL specified. Use --url <url>" << std::endl;
        printUsage(argv[0]);
        return 1;
    }
    
    // Build version-specific directory
    std::string targetDir = versionDir + "/" + versionName;
    std::string tempAPK = targetDir + "/temp.apk";
    
    std::cout << "[Downloader] Target directory: " << targetDir << std::endl;
    
    // Ensure directories exist
    if (ensureDir(targetDir.c_str()) != 0) {
        std::cerr << "[Downloader] FATAL: Cannot create version directory: " << targetDir << std::endl;
        return 1;
    }
    
    // Initialize curl
    curl_global_init(CURL_GLOBAL_DEFAULT);
    
    // Download APK
    if (!downloadFile(apkUrl, tempAPK)) {
        std::cerr << "[Downloader] FATAL: Download failed" << std::endl;
        return 1;
    }
    
    // Extract from APK
    if (!extractFromAPK(tempAPK, targetDir)) {
        std::cerr << "[Downloader] FATAL: Extraction failed" << std::endl;
        cleanupAPK(tempAPK);
        return 1;
    }
    
    // Cleanup
    cleanupAPK(tempAPK);
    
    // Create version info file
    std::string versionInfoPath = targetDir + "/version.info";
    FILE* versionInfo = fopen(versionInfoPath.c_str(), "w");
    if (versionInfo) {
        fprintf(versionInfo, "version=%s\n", versionName.c_str());
        fprintf(versionInfo, "url=%s\n", apkUrl.c_str());
        fclose(versionInfo);
    }
    
    std::cout << std::endl;
    std::cout << "========================================" << std::endl;
    std::cout << "  Download complete!" << std::endl;
    std::cout << "  Version: " << versionName << std::endl;
    std::cout << "  Location: " << targetDir << std::endl;
    std::cout << "========================================" << std::endl;
    
    return 0;
}