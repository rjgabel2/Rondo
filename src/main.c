#include "gb.h"

#define SDL_MAIN_HANDLED

#include "SDL.h"

#include "stdbool.h"
#include "stdio.h"

SDL_Window* window;
SDL_Surface* framebuf;
// Needed because you can't do a ScaledBlit directly from an 8-bit indexed
// surface
SDL_Surface* tempbuf;
u8* rom;
GameBoy* gb;

SDL_Color master_palette[4] = {{0xFF, 0xFF, 0xFF, 0xFF},
                               {0xAA, 0xAA, 0xAA, 0xFF},
                               {0x55, 0x55, 0x55, 0xFF},
                               {0x00, 0x00, 0x00, 0xFF}};

static void try_sdl_func(int line, bool cond) {
    if (cond) {
        printf("ERROR (line %d): %s\n", line, SDL_GetError());
        exit(1);
    }
}

#define try_sdl(cond) try_sdl_func(__LINE__, cond)

static SDL_Rect get_dest_rect(SDL_Surface* surf) {
    int w = surf->w;
    int h = surf->h;

    if (10 * h < 9 * w) {
        // Height is limiting
        int tw = 10 * h / 9;
        int margin = (w - tw) / 2;
        SDL_Rect r = {margin, 0, tw, h};
        return r;
    } else if (9 * w < 10 * h) {
        // Width is limiting
        int th = 9 * w / 10;
        int margin = (h - th) / 2;
        SDL_Rect r = {0, margin, w, th};
        return r;
    } else {
        SDL_Rect r = {0, 0, w, h};
        return r;
    }
}

static void draw() {
    if (framebuf) {
        SDL_UnlockSurface(framebuf);
        try_sdl(SDL_BlitSurface(framebuf, NULL, tempbuf, NULL));
        try_sdl(SDL_LockSurface(framebuf));

        SDL_Surface* win_surf = SDL_GetWindowSurface(window);
        SDL_Rect r = get_dest_rect(win_surf);

        try_sdl(SDL_BlitScaled(tempbuf, NULL, win_surf, &r));
    } else {
        SDL_Surface* win_surf = SDL_GetWindowSurface(window);
        try_sdl(SDL_FillRect(win_surf, NULL, 0));
    }

    try_sdl(SDL_UpdateWindowSurface(window));
}

static void init() {
    try_sdl(SDL_Init(SDL_INIT_AUDIO | SDL_INIT_VIDEO));

    window = SDL_CreateWindow("Rondo", SDL_WINDOWPOS_UNDEFINED,
                              SDL_WINDOWPOS_UNDEFINED, SCREEN_WIDTH,
                              SCREEN_HEIGHT, SDL_WINDOW_RESIZABLE);
    try_sdl(!window);
}

static void quit() {
    destroy_gb(gb);
    SDL_UnlockSurface(framebuf);
    SDL_FreeSurface(framebuf);
    SDL_FreeSurface(tempbuf);
    SDL_free(rom);
    SDL_DestroyWindow(window);
    SDL_Quit();
    exit(0);
}

static void event_loop() {
    SDL_Event e;
    while (SDL_PollEvent(&e)) {
        switch (e.type) {
        case SDL_QUIT:
            quit();
            break;
        case SDL_WINDOWEVENT: {
            SDL_WindowEvent we = e.window;
            if (we.event == SDL_WINDOWEVENT_EXPOSED) {
                draw();
            }
            break;
        }
        }
    }
}

static void load_rom(char* filename) {
    size_t size;
    rom = SDL_LoadFile(filename, &size);

    if (!rom) {
        printf("Error: could not load file %s\n", filename);
        exit(0);
    }

    gb = make_gb(rom, size);
    if (!gb) {
        // Initialization must have failed
        printf("Failed to init rom\n");
        exit(0);
    }

    // Create framebuffer
    if (gb->type == DMG) {
        framebuf = SDL_CreateRGBSurfaceWithFormat(
            0, SCREEN_WIDTH, SCREEN_HEIGHT, 8, SDL_PIXELFORMAT_INDEX8);
        try_sdl(!framebuf);
        try_sdl(SDL_SetPaletteColors(framebuf->format->palette, master_palette,
                                     0, 4));
        // Clear framebuf
        try_sdl(SDL_FillRect(framebuf, NULL, 0));

        tempbuf = SDL_ConvertSurface(framebuf,
                                     SDL_GetWindowSurface(window)->format, 0);
        try_sdl(!tempbuf);
        try_sdl(SDL_LockSurface(framebuf));
    } else {
        printf("Non-DMG not yet supported\n");
        exit(1);
    }

    gb->fbuf = framebuf->pixels;
}

int main(int argc, char* argv[]) {
    if (argc != 2) {
        printf("Usage: rondo.exe [filename]\n");
        exit(0);
    }

    init();
    load_rom(argv[1]);

    while (true) {
        // All timing is in millicseconds
        Uint64 start = SDL_GetTicks64();
        event_loop();
        run_frame(gb);
        draw();
        Uint64 end = SDL_GetTicks64();
        Uint64 elapsed = end - start;
        Uint64 target = (1000 * gb->cycles) >> 22;
        if (elapsed < target) {
            SDL_Delay(target - elapsed);
        }
        gb->cycles = 0;
    }
}