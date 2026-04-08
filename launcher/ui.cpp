/**
 * SDL2 Launcher UI - Main launcher interface for Raspberry Pi
 * 
 * Minimal SDL2 UI for:
 * - Listing downloaded versions
 * - Launching selected version
 * - Downloading new versions
 * 
 * Pi-specific: 800x480 touchscreen resolution, software renderer only
 * Memory target: under 20MB RAM
 * 
 * IMPORTANT: No fork() - uses exec() directly after SDL cleanup
 */

#include <SDL2/SDL.h>
#include <SDL2/SDL_ttf.h>
#include <SDL2/SDL_error.h>
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>
#include <sys/wait.h>
#include <cstring>
#include <iostream>
#include <vector>
#include <string>
#include <algorithm>

// ============================================================================
// Configuration
// ============================================================================

// Pi-friendly resolution (touchscreen)
static const int WINDOW_WIDTH = 800;
static const int WINDOW_HEIGHT = 480;

// Paths
static const char* VERSION_DIR = "~/.local/share/mcpe-launcher/versions";
static const char* LAUNCHER_BIN = "/usr/local/bin/pi-loader";
static const char* DOWNLOADER_BIN = "/usr/local/bin/pi-downloader";

// ============================================================================
// Data Structures
// ============================================================================

struct Version {
    std::string name;
    std::string path;
    bool valid;
};

struct Button {
    int x, y, w, h;
    std::string text;
    bool hovered;
    bool clicked;
};

// ============================================================================
// Globals (minimal footprint)
// ============================================================================

static SDL_Window* g_window = nullptr;
static SDL_Renderer* g_renderer = nullptr;
static TTF_Font* g_font = nullptr;
static TTF_Font* g_fontSmall = nullptr;

static std::vector<Version> g_versions;
static int g_selectedVersion = -1;
static std::string g_statusMessage;

// Download dialog state
static bool g_showDownloadDialog = false;
static std::string g_downloadUrl;
static std::string g_inputBuffer;

// ============================================================================
// Helper Functions
// ============================================================================

/**
 * Expand ~ to HOME directory
 */
static std::string expandPath(const char* path) {
    if (!path) return "";
    if (path[0] == '~' && path[1] == '/') {
        const char* home = getenv("HOME");
        if (home) return std::string(home) + (path + 1);
    }
    return std::string(path);
}

/**
 * Scan for downloaded versions
 */
static void scanVersions() {
    g_versions.clear();
    
    std::string versionDir = expandPath(VERSION_DIR);
    DIR* dir = opendir(versionDir.c_str());
    if (!dir) return;
    
    struct dirent* entry;
    while ((entry = readdir(dir)) != nullptr) {
        if (entry->d_type == DT_DIR && 
            strcmp(entry->d_name, ".") != 0 && 
            strcmp(entry->d_name, "..") != 0) {
            
            std::string path = versionDir + "/" + entry->d_name;
            std::string libPath = path + "/libminecraftpe.so";
            
            struct stat st;
            bool valid = (stat(libPath.c_str(), &st) == 0);
            
            Version v;
            v.name = entry->d_name;
            v.path = path;
            v.valid = valid;
            
            g_versions.push_back(v);
        }
    }
    
    closedir(dir);
    
    // Sort by name (newest first if version naming is semantic)
    std::sort(g_versions.begin(), g_versions.end(), 
              [](const Version& a, const Version& b) {
                  return a.name > b.name;
              });
}

/**
 * Render text to surface
 */
static SDL_Texture* renderText(const std::string& text, TTF_Font* font, SDL_Color color) {
    if (!font || text.empty()) return nullptr;
    
    SDL_Surface* surface = TTF_RenderUTF8_Blended(font, text.c_str(), color);
    if (!surface) return nullptr;
    
    SDL_Texture* texture = SDL_CreateTextureFromSurface(g_renderer, surface);
    SDL_FreeSurface(surface);
    
    return texture;
}

/**
 * Draw a button
 */
static bool drawButton(Button& btn, SDL_Color bgColor, SDL_Color textColor) {
    // Draw background
    SDL_SetRenderDrawColor(g_renderer, bgColor.r, bgColor.g, bgColor.b, bgColor.a);
    SDL_Rect rect = {btn.x, btn.y, btn.w, btn.h};
    SDL_RenderFillRect(g_renderer, &rect);
    
    // Draw border
    SDL_SetRenderDrawColor(g_renderer, textColor.r, textColor.g, textColor.b, textColor.a);
    SDL_RenderDrawRect(g_renderer, &rect);
    
    // Draw text centered
    if (g_font) {
        SDL_Texture* textTex = renderText(btn.text, g_font, textColor);
        if (textTex) {
            int tw, th;
            SDL_QueryTexture(textTex, nullptr, nullptr, &tw, &th);
            
            SDL_Rect textRect = {
                btn.x + (btn.w - tw) / 2,
                btn.y + (btn.h - th) / 2,
                tw, th
            };
            
            SDL_RenderCopy(g_renderer, textTex, nullptr, &textRect);
            SDL_DestroyTexture(textTex);
        }
    }
    
    return btn.clicked;
}

/**
 * Check if point is in rect
 */
static bool pointInRect(int px, int py, const SDL_Rect& rect) {
    return px >= rect.x && px < rect.x + rect.w &&
           py >= rect.y && py < rect.y + rect.h;
}

/**
 * Launch game - fully exit UI first, then exec the loader
 * 
 * CRITICAL: This must NOT use fork(). We exec() directly after cleanup.
 * This ensures no child process carries UI memory.
 */
static void launchGame(const Version& version) {
    std::cout << "[UI] Launching version: " << version.name << std::endl;
    std::cout << "[UI] Game path: " << version.path << std::endl;
    
    // Cleanup SDL completely BEFORE exec
    std::cout << "[UI] Cleaning up SDL..." << std::endl;
    
    if (g_font) {
        TTF_CloseFont(g_font);
        g_font = nullptr;
    }
    if (g_fontSmall) {
        TTF_CloseFont(g_fontSmall);
        g_fontSmall = nullptr;
    }
    
    if (g_renderer) {
        SDL_DestroyRenderer(g_renderer);
        g_renderer = nullptr;
    }
    if (g_window) {
        SDL_DestroyWindow(g_window);
        g_window = nullptr;
    }
    
    TTF_Quit();
    SDL_Quit();
    
    // Set environment for loader
    setenv("MCPE_LIB_PATH", (version.path + "/libminecraftpe.so").c_str(), 1);
    setenv("MCPE_WORKING_DIR", version.path.c_str(), 1);
    setenv("MCPE_ASSETS_DIR", (version.path + "/assets").c_str(), 1);
    
    // exec() the loader - replaces this process entirely
    // No return, no fork
    std::cout << "[UI] Executing loader..." << std::endl;
    execl(LAUNCHER_BIN, "pi-loader", nullptr);
    
    // If we get here, exec failed
    std::cerr << "[UI] FATAL: exec() failed: " << strerror(errno) << std::endl;
    exit(1);
}

/**
 * Run downloader as separate process
 */
static void runDownloader(const std::string& url, const std::string& version) {
    // Use fork+exec for downloader (it needs to run independently)
    // This is OK because downloader is a small process and we need its exit code
    pid_t pid = fork();
    
    if (pid < 0) {
        g_statusMessage = "Failed to start downloader";
        return;
    }
    
    if (pid == 0) {
        // Child process - exec downloader
        execl(DOWNLOADER_BIN, "pi-downloader", 
              "--url", url.c_str(), 
              "--version", version.c_str(),
              nullptr);
        
        // If exec fails, exit child
        std::cerr << "[UI] Downloader exec failed" << std::endl;
        exit(1);
    }
    
    // Parent - wait for download to complete
    int status;
    waitpid(pid, &status, 0);
    
    if (WIFEXITED(status) && WEXITSTATUS(status) == 0) {
        g_statusMessage = "Download complete!";
        scanVersions();  // Refresh version list
    } else {
        g_statusMessage = "Download failed";
    }
}

// ============================================================================
// UI Rendering
// ============================================================================

/**
 * Render main UI
 */
static void render() {
    // Clear background (dark gray)
    SDL_SetRenderDrawColor(g_renderer, 40, 40, 40, 255);
    SDL_RenderClear(g_renderer);
    
    // Title bar
    SDL_SetRenderDrawColor(g_renderer, 60, 60, 80, 255);
    SDL_Rect titleBar = {0, 0, WINDOW_WIDTH, 50};
    SDL_RenderFillRect(g_renderer, &titleBar);
    
    // Title text
    SDL_Color white = {255, 255, 255, 255};
    SDL_Texture* titleTex = renderText("Pi Minecraft Launcher", g_font, white);
    if (titleTex) {
        SDL_Rect titleRect = {20, 10, 300, 30};
        SDL_RenderCopy(g_renderer, titleTex, nullptr, &titleRect);
        SDL_DestroyTexture(titleTex);
    }
    
    // Version list
    SDL_Color listBg = {50, 50, 50, 255};
    SDL_Rect listRect = {20, 60, WINDOW_WIDTH - 40, WINDOW_HEIGHT - 160};
    SDL_SetRenderDrawColor(g_renderer, listBg.r, listBg.g, listBg.b, listBg.a);
    SDL_RenderFillRect(g_renderer, &listRect);
    
    // Version items
    SDL_Color itemColor = {200, 200, 200, 255};
    SDL_Color selectedColor = {100, 150, 255, 255};
    SDL_Color invalidColor = {150, 100, 100, 255};
    
    int y = 70;
    for (size_t i = 0; i < g_versions.size() && y < listRect.h - 30; ++i) {
        SDL_Color color = g_versions[i].valid ? 
            (i == g_selectedVersion ? selectedColor : itemColor) : invalidColor;
        
        // Draw selection highlight
        if (i == g_selectedVersion) {
            SDL_SetRenderDrawColor(g_renderer, 80, 80, 120, 255);
            SDL_Rect selRect = {25, y - 5, WINDOW_WIDTH - 50, 35};
            SDL_RenderFillRect(g_renderer, &selRect);
        }
        
        // Version name
        std::string displayName = g_versions[i].name;
        if (!g_versions[i].valid) displayName += " (invalid)";
        
        SDL_Texture* versionTex = renderText(displayName, g_fontSmall, color);
        if (versionTex) {
            SDL_Rect verRect = {35, y, 200, 25};
            SDL_RenderCopy(g_renderer, versionTex, nullptr, &verRect);
            SDL_DestroyTexture(versionTex);
        }
        
        y += 40;
    }
    
    if (g_versions.empty()) {
        SDL_Texture* emptyTex = renderText("No versions installed", g_fontSmall, itemColor);
        if (emptyTex) {
            SDL_Rect emptyRect = {35, 70, 200, 25};
            SDL_RenderCopy(g_renderer, emptyTex, nullptr, &emptyRect);
            SDL_DestroyTexture(emptyTex);
        }
    }
    
    // Bottom buttons
    int buttonY = WINDOW_HEIGHT - 90;
    
    // Launch button
    Button launchBtn = {20, buttonY, 200, 50, "Launch", false, false};
    SDL_Color launchBg = {50, 150, 50, 255};
    SDL_Color launchText = {255, 255, 255, 255};
    drawButton(launchBtn, launchBg, launchText);
    
    // Download button
    Button downloadBtn = {240, buttonY, 250, 50, "Download Version", false, false};
    SDL_Color downloadBg = {50, 100, 180, 255};
    drawButton(downloadBtn, downloadBg, launchText);
    
    // Refresh button
    Button refreshBtn = {510, buttonY, 130, 50, "Refresh", false, false};
    SDL_Color refreshBg = {100, 100, 100, 255};
    drawButton(refreshBtn, refreshBg, launchText);
    
    // Quit button
    Button quitBtn = {660, buttonY, 120, 50, "Quit", false, false};
    SDL_Color quitBg = {180, 60, 60, 255};
    drawButton(quitBtn, quitBg, launchText);
    
    // Status message
    if (!g_statusMessage.empty()) {
        SDL_Texture* statusTex = renderText(g_statusMessage, g_fontSmall, white);
        if (statusTex) {
            SDL_Rect statusRect = {20, WINDOW_HEIGHT - 25, 400, 20};
            SDL_RenderCopy(g_renderer, statusTex, nullptr, &statusRect);
            SDL_DestroyTexture(statusTex);
        }
    }
    
    // Download dialog
    if (g_showDownloadDialog) {
        // Darken background
        SDL_SetRenderDrawColor(g_renderer, 0, 0, 0, 150);
        SDL_Rect overlay = {0, 0, WINDOW_WIDTH, WINDOW_HEIGHT};
        SDL_RenderFillRect(g_renderer, &overlay);
        
        // Dialog box
        SDL_SetRenderDrawColor(g_renderer, 60, 60, 60, 255);
        SDL_Rect dialogRect = {100, 100, WINDOW_WIDTH - 200, 280};
        SDL_RenderFillRect(g_renderer, &dialogRect);
        SDL_SetRenderDrawColor(g_renderer, 100, 100, 100, 255);
        SDL_RenderDrawRect(g_renderer, &dialogRect);
        
        // Dialog title
        SDL_Texture* dialogTitle = renderText("Download Version", g_font, white);
        if (dialogTitle) {
            SDL_Rect titleRect = {120, 110, 300, 30};
            SDL_RenderCopy(g_renderer, dialogTitle, nullptr, &titleRect);
            SDL_DestroyTexture(dialogTitle);
        }
        
        // URL input label
        SDL_Texture* urlLabel = renderText("APK URL:", g_fontSmall, white);
        if (urlLabel) {
            SDL_Rect labelRect = {120, 160, 100, 20};
            SDL_RenderCopy(g_renderer, urlLabel, nullptr, &labelRect);
            SDL_DestroyTexture(urlLabel);
        }
        
        // URL input field
        SDL_SetRenderDrawColor(g_renderer, 30, 30, 30, 255);
        SDL_Rect inputRect = {120, 185, WINDOW_WIDTH - 240, 30};
        SDL_RenderFillRect(g_renderer, &inputRect);
        
        if (!g_inputBuffer.empty()) {
            SDL_Texture* inputTex = renderText(g_inputBuffer, g_fontSmall, white);
            if (inputTex) {
                SDL_Rect inputDisplayRect = {125, 188, WINDOW_WIDTH - 250, 24};
                SDL_RenderCopy(g_renderer, inputTex, nullptr, &inputDisplayRect);
                SDL_DestroyTexture(inputTex);
            }
        }
        
        // Version name label
        SDL_Texture* verLabel = renderText("Version name:", g_fontSmall, white);
        if (verLabel) {
            SDL_Rect verLabelRect = {120, 230, 150, 20};
            SDL_RenderCopy(g_renderer, verLabel, nullptr, &verLabelRect);
            SDL_DestroyTexture(verLabel);
        }
        
        // Version input
        SDL_Rect verInputRect = {120, 255, 200, 30};
        SDL_SetRenderDrawColor(g_renderer, 30, 30, 30, 255);
        SDL_RenderFillRect(g_renderer, &verInputRect);
        
        // Dialog buttons
        Button okBtn = {WINDOW_WIDTH - 320, 310, 100, 40, "OK", false, false};
        Button cancelBtn = {WINDOW_WIDTH - 210, 310, 100, 40, "Cancel", false, false};
        
        SDL_Color okBg = {50, 150, 50, 255};
        SDL_Color cancelBg = {150, 60, 60, 255};
        
        drawButton(okBtn, okBg, white);
        drawButton(cancelBtn, cancelBg, white);
    }
    
    SDL_RenderPresent(g_renderer);
}

// ============================================================================
// Event Handling
// ============================================================================

/**
 * Handle mouse click
 */
static void handleClick(int mx, int my) {
    if (g_showDownloadDialog) {
        // Dialog button clicks
        // OK button
        if (mx >= WINDOW_WIDTH - 320 && mx < WINDOW_WIDTH - 220 &&
            my >= 310 && my < 350) {
            
            if (!g_inputBuffer.empty()) {
                // Parse version name from URL or use default
                std::string version = "default";
                size_t lastSlash = g_inputBuffer.rfind('/');
                if (lastSlash != std::string::npos) {
                    std::string filename = g_inputBuffer.substr(lastSlash + 1);
                    if (filename.find(".apk") != std::string::npos) {
                        version = filename.substr(0, filename.find(".apk"));
                    }
                }
                
                runDownloader(g_inputBuffer, version);
            }
            
            g_showDownloadDialog = false;
            g_inputBuffer.clear();
            return;
        }
        
        // Cancel button
        if (mx >= WINDOW_WIDTH - 210 && mx < WINDOW_WIDTH - 110 &&
            my >= 310 && my < 350) {
            
            g_showDownloadDialog = false;
            g_inputBuffer.clear();
            return;
        }
        
        return;
    }
    
    // Version list selection
    if (mx >= 20 && mx < WINDOW_WIDTH - 20 &&
        my >= 60 && my < WINDOW_HEIGHT - 160) {
        
        int clickedIndex = (my - 70) / 40;
        if (clickedIndex >= 0 && clickedIndex < (int)g_versions.size()) {
            g_selectedVersion = clickedIndex;
        }
        return;
    }
    
    // Launch button
    if (mx >= 20 && mx < 220 && my >= WINDOW_HEIGHT - 90 && my < WINDOW_HEIGHT - 40) {
        if (g_selectedVersion >= 0 && g_selectedVersion < (int)g_versions.size()) {
            const Version& v = g_versions[g_selectedVersion];
            if (v.valid) {
                launchGame(v);
            } else {
                g_statusMessage = "Invalid version - redownload";
            }
        } else {
            g_statusMessage = "Select a version first";
        }
        return;
    }
    
    // Download button
    if (mx >= 240 && mx < 490 && my >= WINDOW_HEIGHT - 90 && my < WINDOW_HEIGHT - 40) {
        g_showDownloadDialog = true;
        g_inputBuffer.clear();
        return;
    }
    
    // Refresh button
    if (mx >= 510 && mx < 640 && my >= WINDOW_HEIGHT - 90 && my < WINDOW_HEIGHT - 40) {
        scanVersions();
        g_statusMessage = "Refreshed version list";
        return;
    }
    
    // Quit button
    if (mx >= 660 && mx < 780 && my >= WINDOW_HEIGHT - 90 && my < WINDOW_HEIGHT - 40) {
        // Cleanup and exit
        std::cout << "[UI] User requested quit" << std::endl;
        return;
    }
}

/**
 * Handle text input (for download dialog)
 */
static void handleTextInput(const char* text) {
    if (!g_showDownloadDialog) return;
    
    // Add characters to input buffer
    g_inputBuffer += text;
}

/**
 * Handle key press
 */
static void handleKeyPress(SDL_Keycode key) {
    if (g_showDownloadDialog) {
        if (key == SDLK_BACKSPACE) {
            if (!g_inputBuffer.empty()) {
                g_inputBuffer.pop_back();
            }
        } else if (key == SDLK_ESCAPE) {
            g_showDownloadDialog = false;
            g_inputBuffer.clear();
        } else if (key == SDLK_RETURN || key == SDLK_KP_ENTER) {
            // Treat Enter as OK click
            if (!g_inputBuffer.empty()) {
                std::string version = "default";
                size_t lastSlash = g_inputBuffer.rfind('/');
                if (lastSlash != std::string::npos) {
                    std::string filename = g_inputBuffer.substr(lastSlash + 1);
                    if (filename.find(".apk") != std::string::npos) {
                        version = filename.substr(0, filename.find(".apk"));
                    }
                }
                
                runDownloader(g_inputBuffer, version);
            }
            
            g_showDownloadDialog = false;
            g_inputBuffer.clear();
        }
    }
}

// ============================================================================
// Main
// ============================================================================

int main(int argc, char** argv) {
    (void)argc;
    (void)argv;
    
    std::cout << "========================================" << std::endl;
    std::cout << "  Pi Minecraft Launcher v1.0.0" << std::endl;
    std::cout << "  For Raspberry Pi 4 (arm64)" << std::endl;
    std::cout << "========================================" << std::endl;
    
    // Initialize SDL
    if (SDL_Init(SDL_INIT_VIDEO) < 0) {
        std::cerr << "[UI] FATAL: SDL_Init failed: " << SDL_GetError() << std::endl;
        return 1;
    }
    
    // Initialize SDL_ttf
    if (TTF_Init() < 0) {
        std::cerr << "[UI] FATAL: TTF_Init failed: " << TTF_GetError() << std::endl;
        SDL_Quit();
        return 1;
    }
    
    // Create window (software renderer for Pi compatibility)
    g_window = SDL_CreateWindow(
        "Pi Minecraft Launcher",
        SDL_WINDOWPOS_CENTERED,
        SDL_WINDOWPOS_CENTERED,
        WINDOW_WIDTH,
        WINDOW_HEIGHT,
        SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE
    );
    
    if (!g_window) {
        std::cerr << "[UI] FATAL: SDL_CreateWindow failed: " << SDL_GetError() << std::endl;
        TTF_Quit();
        SDL_Quit();
        return 1;
    }
    
    // Create software renderer (no GPU required)
    g_renderer = SDL_CreateRenderer(g_window, -1, SDL_RENDERER_SOFTWARE);
    if (!g_renderer) {
        std::cerr << "[UI] FATAL: SDL_CreateRenderer failed: " << SDL_GetError() << std::endl;
        SDL_DestroyWindow(g_window);
        TTF_Quit();
        SDL_Quit();
        return 1;
    }
    
    // Try to load a font (fallback to built-in if no system fonts)
    g_font = TTF_OpenFont("/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf", 20);
    g_fontSmall = TTF_OpenFont("/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf", 14);
    
    if (!g_font) {
        // Try alternate paths
        g_font = TTF_OpenFont("/usr/share/fonts/dejavu/DejaVuSans.ttf", 20);
        g_fontSmall = TTF_OpenFont("/usr/share/fonts/dejavu/DejaVuSans.ttf", 14);
    }
    
    if (!g_font) {
        std::cerr << "[UI] WARNING: Could not load font - text will not display" << std::endl;
    }
    
    // Scan for versions
    scanVersions();
    
    std::cout << "[UI] Found " << g_versions.size() << " versions" << std::endl;
    
    // Main event loop
    SDL_Event event;
    bool running = true;
    
    while (running) {
        // Handle events
        while (SDL_PollEvent(&event)) {
            switch (event.type) {
                case SDL_QUIT:
                    running = false;
                    break;
                    
                case SDL_MOUSEBUTTONDOWN:
                    if (event.button.button == SDL_BUTTON_LEFT) {
                        handleClick(event.button.x, event.button.y);
                    }
                    break;
                    
                case SDL_TEXTINPUT:
                    handleTextInput(event.text.text);
                    break;
                    
                case SDL_KEYDOWN:
                    handleKeyPress(event.key.keysym.sym);
                    break;
            }
        }
        
        // Render
        render();
        
        // Small delay to reduce CPU usage
        SDL_Delay(16);  // ~60 FPS max
    }
    
    // Cleanup
    std::cout << "[UI] Shutting down..." << std::endl;
    
    if (g_font) TTF_CloseFont(g_font);
    if (g_fontSmall) TTF_CloseFont(g_fontSmall);
    if (g_renderer) SDL_DestroyRenderer(g_renderer);
    if (g_window) SDL_DestroyWindow(g_window);
    
    TTF_Quit();
    SDL_Quit();
    
    std::cout << "[UI] Goodbye!" << std::endl;
    return 0;
}