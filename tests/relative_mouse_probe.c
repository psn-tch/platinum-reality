// A game-shaped SDL3 client: relative mouse mode exactly as games request it, over X11 or
// Wayland (SDL_VIDEO_DRIVER). Commands on stdin: "relative 1", "relative 0", "quit", and
// "exclusive" (full screen in a smaller display mode, as games that change the resolution do)
// and "windowed".
// Prints: ready DRIVER, focus, relative-mode 0|1, motion X Y XREL YREL, exclusive-requested W H
// (or no-smaller-mode), fullscreen-entered W H (the window's pixels), fullscreen-left.
#include <SDL3/SDL.h>
#include <poll.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

static void say(const char *text)
{
    puts(text);
    fflush(stdout);
}

int main(void)
{
    if (!SDL_Init(SDL_INIT_VIDEO))
    {
        printf("sdl-error %s\n", SDL_GetError());
        return 2;
    }
    SDL_Window *window = SDL_CreateWindow("Relative Mouse Probe", 320, 240, 0);
    if (!window)
    {
        printf("sdl-error %s\n", SDL_GetError());
        return 2;
    }
    char line[256];
    snprintf(line, sizeof line, "ready %s", SDL_GetCurrentVideoDriver());
    say(line);
    char input[1024];
    size_t pending = 0;
    int running = 1;
    while (running)
    {
        SDL_Surface *surface = SDL_GetWindowSurface(window);
        if (surface)
        {
            SDL_FillSurfaceRect(surface, NULL, SDL_MapSurfaceRGB(surface, 0xa0, 0x60, 0x30));
            SDL_UpdateWindowSurface(window);
        }
        SDL_Event event;
        if (SDL_WaitEventTimeout(&event, 10))
            do
            {
                if (event.type == SDL_EVENT_QUIT || event.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED)
                    running = 0;
                else if (event.type == SDL_EVENT_WINDOW_FOCUS_GAINED)
                    say("focus");
                else if (event.type == SDL_EVENT_WINDOW_ENTER_FULLSCREEN)
                {
                    int w = 0, h = 0;
                    SDL_GetWindowSizeInPixels(window, &w, &h);
                    snprintf(line, sizeof line, "fullscreen-entered %d %d", w, h);
                    say(line);
                }
                else if (event.type == SDL_EVENT_WINDOW_LEAVE_FULLSCREEN)
                    say("fullscreen-left");
                else if (event.type == SDL_EVENT_MOUSE_MOTION)
                {
                    snprintf(line, sizeof line, "motion %.1f %.1f %.1f %.1f", event.motion.x, event.motion.y,
                             event.motion.xrel, event.motion.yrel);
                    say(line);
                }
            } while (SDL_PollEvent(&event));
        struct pollfd stdinPoll = {STDIN_FILENO, POLLIN, 0};
        if (poll(&stdinPoll, 1, 0) > 0)
        {
            const ssize_t count = read(STDIN_FILENO, input + pending, sizeof input - 1 - pending);
            if (count <= 0)
                break;
            pending += (size_t)count;
            input[pending] = '\0';
            char *start = input, *end;
            while ((end = strchr(start, '\n')))
            {
                *end = '\0';
                if (!strcmp(start, "quit"))
                    running = 0;
                else if (!strcmp(start, "exclusive"))
                {
                    // The desktop's mode, then the largest listed mode smaller than it (at least 640 wide).
                    const SDL_DisplayID display = SDL_GetDisplayForWindow(window);
                    const SDL_DisplayMode *desktop = SDL_GetDesktopDisplayMode(display);
                    int count = 0;
                    SDL_DisplayMode **modes = SDL_GetFullscreenDisplayModes(display, &count);
                    const SDL_DisplayMode *chosen = NULL;
                    for (int i = 0; modes && i < count; ++i)
                        if (desktop && modes[i]->w < desktop->w && modes[i]->h < desktop->h && modes[i]->w >= 640 &&
                            (!chosen || modes[i]->w > chosen->w))
                            chosen = modes[i];
                    if (chosen && SDL_SetWindowFullscreenMode(window, chosen) && SDL_SetWindowFullscreen(window, true))
                        snprintf(line, sizeof line, "exclusive-requested %d %d", chosen->w, chosen->h);
                    else
                        snprintf(line, sizeof line, "no-smaller-mode %d %s", count, SDL_GetError());
                    say(line);
                    SDL_free(modes);
                }
                else if (!strcmp(start, "windowed"))
                    SDL_SetWindowFullscreen(window, false);
                else if (!strncmp(start, "relative ", 9))
                {
                    SDL_SetWindowRelativeMouseMode(window, start[9] == '1');
                    snprintf(line, sizeof line, "relative-mode %d", SDL_GetWindowRelativeMouseMode(window));
                    say(line);
                }
                start = end + 1;
            }
            pending = strlen(start);
            memmove(input, start, pending);
        }
    }
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}
