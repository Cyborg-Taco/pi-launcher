#include <SDL2/SDL.h>
#include <iostream>
#include <string>
#include <unistd.h>

extern void download_version(const std::string& url, const std::string& target_dir);
extern int main_run_game(const std::string& so_path, int argc, char** argv);

int main(int argc, char** argv) {
    if (argc >= 3 && std::string(argv[1]) == "--launch") {
        // Core Loader pipeline
        return main_run_game(argv[2], argc - 2, argv + 2);
    }

    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
        std::cerr << "SDL_Init Error: " << SDL_GetError() << std::endl;
        return 1;
    }

    // SDL window restricted to Pi touchscreen hardware target specs.
    // Software rendering bypasses Mesa locks while in the graphical launcher stage.
    SDL_Window* window = SDL_CreateWindow("Minecraft Bedrock Pi Launcher", 
                                          SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, 
                                          800, 480, SDL_WINDOW_SHOWN);
    SDL_Surface* screen = SDL_GetWindowSurface(window);

    bool running = true;
    std::string launch_path = "";
    std::string default_dir = std::string(getenv("HOME") ? getenv("HOME") : "/home/pi") + "/.local/share/mcpe-launcher/versions/latest";

    while (running) {
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_QUIT) running = false;
            if (e.type == SDL_MOUSEBUTTONDOWN) {
                if (e.button.x > 400) {
                    launch_path = default_dir + "/lib/arm64-v8a/libminecraftpe.so";
                    running = false;
                } else {
                    // Minimal UI prompts for URL in the bound background runtime
                    std::cout << "\n[Console Action] Target Download APK -> Enter URL: ";
                    std::string url;
                    std::cin >> url;
                    download_version(url, default_dir);
                }
            }
        }
        
        SDL_FillRect(screen, NULL, SDL_MapRGB(screen->format, 40, 40, 40));
        
        // Draw left layout block (Download)
        SDL_Rect btnDl = {50, 190, 300, 100};
        SDL_FillRect(screen, &btnDl, SDL_MapRGB(screen->format, 50, 160, 50));
        
        // Draw right layout block (Launch)
        SDL_Rect btnLaunch = {450, 190, 300, 100};
        SDL_FillRect(screen, &btnLaunch, SDL_MapRGB(screen->format, 50, 50, 160));
        
        SDL_UpdateWindowSurface(window);
        SDL_Delay(50);
    }

    // HARD CONSTRAINT: The UI process must fully exit and free all its memory
    // before exec()-ing the game — do not fork.
    SDL_DestroyWindow(window);
    SDL_Quit();

    if (!launch_path.empty()) {
        std::cout << "Terminating UI container footprint... Exec-ing loader sequence..." << std::endl;
        execl(argv[0], argv[0], "--launch", launch_path.c_str(), (char*)NULL);
    }

    return 0;
}
