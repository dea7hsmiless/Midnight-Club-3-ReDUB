#include <SDL.h>
int main(int argc, char **argv) {
    if (argc < 2) return 2;
    return SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "ReDUB", argv[1], 0) < 0;
}
