// A raw Wayland client with two windows, "Activation Probe A" and "Activation Probe B", that asks
// for xdg-activation tokens and redeems them when told to, so a test controls how late a request
// arrives (as Firefox's re-activation after closing a dialog did). One command per line on stdin:
//   token A|B          ask for a token, naming that window as the one asking    -> token VALUE
//   activate A|B VALUE ask for that window to be brought forward with VALUE   -> sent
//   quit
// Events: ready (both windows mapped).
#define _GNU_SOURCE
#include "xdg-activation-client.h"
#include "xdg-shell-client.h"
#include <poll.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>
#include <wayland-client.h>

struct window
{
    struct wl_surface *surface;
    struct xdg_surface *xdgSurface;
    struct xdg_toplevel *toplevel;
    struct wl_buffer *buffer;
    int mapped;
};

static struct
{
    struct wl_display *display;
    struct wl_compositor *compositor;
    struct wl_shm *shm;
    struct xdg_wm_base *wm;
    struct xdg_activation_v1 *activation;
    struct window windows[2];
    int ready;
    int running;
} app = {.running = 1};

static void say(const char *format, ...)
{
    va_list arguments;
    va_start(arguments, format);
    vprintf(format, arguments);
    va_end(arguments);
    putchar('\n');
    fflush(stdout);
}
static struct wl_buffer *makeBuffer(int width, int height)
{
    const int stride = width * 4, size = stride * height;
    const int fd = memfd_create("activation-probe", MFD_CLOEXEC);
    if (fd < 0 || ftruncate(fd, size) < 0)
        return NULL;
    uint32_t *pixels = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (pixels == MAP_FAILED)
        return NULL;
    for (int i = 0; i < width * height; ++i)
        pixels[i] = 0xffe0e0e0;
    munmap(pixels, size);
    struct wl_shm_pool *pool = wl_shm_create_pool(app.shm, fd, size);
    struct wl_buffer *buffer = wl_shm_pool_create_buffer(pool, 0, width, height, stride, WL_SHM_FORMAT_ARGB8888);
    wl_shm_pool_destroy(pool);
    close(fd);
    return buffer;
}
static struct window *named(const char *name)
{
    return name && !strcmp(name, "A") ? &app.windows[0] : name && !strcmp(name, "B") ? &app.windows[1] : NULL;
}
static void ping(void *data, struct xdg_wm_base *wm, uint32_t serial)
{
    (void)data;
    xdg_wm_base_pong(wm, serial);
}
static const struct xdg_wm_base_listener wmListener = {ping};
static void surfaceConfigure(void *data, struct xdg_surface *surface, uint32_t serial)
{
    struct window *window = data;
    xdg_surface_ack_configure(surface, serial);
    if (!window->buffer)
        window->buffer = makeBuffer(240, 120);
    wl_surface_attach(window->surface, window->buffer, 0, 0);
    wl_surface_damage(window->surface, 0, 0, 240, 120);
    wl_surface_commit(window->surface);
    window->mapped = 1;
    if (!app.ready && app.windows[0].mapped && app.windows[1].mapped)
    {
        app.ready = 1;
        say("ready");
    }
}
static const struct xdg_surface_listener surfaceListener = {surfaceConfigure};
static void toplevelConfigure(void *data, struct xdg_toplevel *toplevel, int32_t width, int32_t height,
                              struct wl_array *states)
{
    (void)data, (void)toplevel, (void)width, (void)height, (void)states;
}
static void toplevelClose(void *data, struct xdg_toplevel *toplevel)
{
    (void)data, (void)toplevel;
    app.running = 0;
}
static void toplevelBounds(void *data, struct xdg_toplevel *toplevel, int32_t width, int32_t height)
{
    (void)data, (void)toplevel, (void)width, (void)height;
}
static void toplevelCapabilities(void *data, struct xdg_toplevel *toplevel, struct wl_array *capabilities)
{
    (void)data, (void)toplevel, (void)capabilities;
}
static const struct xdg_toplevel_listener toplevelListener = {
    .configure = toplevelConfigure, .close = toplevelClose, .configure_bounds = toplevelBounds,
    .wm_capabilities = toplevelCapabilities};
static void tokenDone(void *data, struct xdg_activation_token_v1 *token, const char *value)
{
    (void)data;
    say("token %s", value);
    xdg_activation_token_v1_destroy(token);
}
static const struct xdg_activation_token_v1_listener tokenListener = {tokenDone};
static void command(char *line)
{
    char *verb = strtok(line, " "), *name = strtok(NULL, " "), *value = strtok(NULL, " ");
    struct window *window = named(name);
    if (!verb || !window)
        return;
    if (!strcmp(verb, "token"))
    {
        struct xdg_activation_token_v1 *token = xdg_activation_v1_get_activation_token(app.activation);
        xdg_activation_token_v1_add_listener(token, &tokenListener, NULL);
        xdg_activation_token_v1_set_surface(token, window->surface);
        xdg_activation_token_v1_commit(token);
    }
    else if (!strcmp(verb, "activate") && value)
    {
        xdg_activation_v1_activate(app.activation, value, window->surface);
        say("sent");
    }
    wl_display_flush(app.display);
}
static void global(void *data, struct wl_registry *registry, uint32_t name, const char *interface,
                   uint32_t version)
{
    (void)data;
#define BIND(target, type, wanted) target = wl_registry_bind(registry, name, &type, version < (wanted) ? version : (wanted))
    if (!strcmp(interface, wl_compositor_interface.name))
        BIND(app.compositor, wl_compositor_interface, 4);
    else if (!strcmp(interface, wl_shm_interface.name))
        BIND(app.shm, wl_shm_interface, 1);
    else if (!strcmp(interface, xdg_wm_base_interface.name))
        BIND(app.wm, xdg_wm_base_interface, 1);
    else if (!strcmp(interface, xdg_activation_v1_interface.name))
        BIND(app.activation, xdg_activation_v1_interface, 1);
#undef BIND
}
static void globalRemove(void *data, struct wl_registry *registry, uint32_t name)
{
    (void)data, (void)registry, (void)name;
}
static const struct wl_registry_listener registryListener = {global, globalRemove};
int main(void)
{
    app.display = wl_display_connect(NULL);
    if (!app.display)
    {
        say("no-display");
        return 2;
    }
    struct wl_registry *registry = wl_display_get_registry(app.display);
    wl_registry_add_listener(registry, &registryListener, NULL);
    wl_display_roundtrip(app.display);
    if (!app.compositor || !app.shm || !app.wm || !app.activation)
    {
        say("missing-globals");
        return 2;
    }
    xdg_wm_base_add_listener(app.wm, &wmListener, NULL);
    for (int i = 0; i < 2; ++i)
    {
        struct window *window = &app.windows[i];
        window->surface = wl_compositor_create_surface(app.compositor);
        window->xdgSurface = xdg_wm_base_get_xdg_surface(app.wm, window->surface);
        xdg_surface_add_listener(window->xdgSurface, &surfaceListener, window);
        window->toplevel = xdg_surface_get_toplevel(window->xdgSurface);
        xdg_toplevel_add_listener(window->toplevel, &toplevelListener, window);
        xdg_toplevel_set_title(window->toplevel, i ? "Activation Probe B" : "Activation Probe A");
        xdg_toplevel_set_app_id(window->toplevel, "org.frankenstein.ActivationProbe");
        wl_surface_commit(window->surface);
    }
    char input[4096];
    size_t pending = 0;
    while (app.running)
    {
        while (wl_display_prepare_read(app.display) != 0)
            if (wl_display_dispatch_pending(app.display) < 0)
                goto done;
        wl_display_flush(app.display);
        struct pollfd fds[2] = {{wl_display_get_fd(app.display), POLLIN, 0}, {STDIN_FILENO, POLLIN, 0}};
        if (poll(fds, 2, -1) < 0)
        {
            wl_display_cancel_read(app.display);
            break;
        }
        if (fds[0].revents & POLLIN)
            wl_display_read_events(app.display);
        else
            wl_display_cancel_read(app.display);
        if (wl_display_dispatch_pending(app.display) < 0)
            break;
        if (fds[1].revents & (POLLIN | POLLHUP))
        {
            const ssize_t count = read(STDIN_FILENO, input + pending, sizeof input - 1 - pending);
            if (count <= 0)
                break;
            pending += (size_t)count;
            input[pending] = '\0';
            char *start = input, *end;
            while (app.running && (end = strchr(start, '\n')))
            {
                *end = '\0';
                if (!strcmp(start, "quit"))
                    app.running = 0;
                else
                    command(start);
                start = end + 1;
            }
            pending = strlen(start);
            memmove(input, start, pending);
        }
    }
done:
    if (wl_display_get_error(app.display))
    {
        say("protocol-error");
        return 4;
    }
    wl_display_disconnect(app.display);
    return 0;
}
