// A raw Wayland client that pastes the way a misbehaving application might. It owns every pipe
// it reads from, so a test controls exactly when a reader stalls, closes or drains, with no
// process to kill. It reads one command per line on stdin and prints one event per line:
//   stall N MIME     receive offer N into a pipe that is never read      -> stalling COUNT
//   sync             a wl_display.sync, answered asynchronously            -> synced
//   close N MIME     receive offer N and close the pipe at once           -> closed N
//   read N MIME      receive offer N and read it to the end (15 s limit)  -> data N LENGTH PREFIX UNIFORM
//                                                                          or data N timeout LENGTH
//   release          read every stalled pipe to the end, then close it    -> released BYTES
//   drop             close every stalled pipe unread                       -> dropped COUNT
//   quit
// Events: ready, focus, unfocus, key CODE STATE, selection N (offers are numbered from 0 in the
// order they arrive, and are kept, so an old one can still be used) or selection none.
// Nothing here waits on the compositor except read and close: a compositor stuck writing into a
// stalled pipe must not stop this client from taking the release command that frees it.
// PREFIX is up to 16 leading bytes; UNIFORM is 1 when every byte equals the first.
#define _GNU_SOURCE
#include "xdg-shell-client.h"
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <time.h>
#include <unistd.h>
#include <wayland-client.h>

enum
{
    maximumOffers = 256,
    maximumStalls = 256
};

static struct
{
    struct wl_display *display;
    struct wl_compositor *compositor;
    struct wl_shm *shm;
    struct xdg_wm_base *wm;
    struct wl_seat *seat;
    struct wl_keyboard *keyboard;
    struct wl_data_device_manager *manager;
    struct wl_data_device *device;
    struct wl_surface *surface;
    struct xdg_surface *xdgSurface;
    struct xdg_toplevel *toplevel;
    struct wl_buffer *buffer;
    struct wl_data_offer *offers[maximumOffers];
    int offerCount;
    int stalls[maximumStalls];
    int stallCount;
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
    const int fd = memfd_create("paste-probe", MFD_CLOEXEC);
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

static long long milliseconds(void)
{
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    return now.tv_sec * 1000LL + now.tv_nsec / 1000000;
}

// Reads fd to its end, appending at most keep bytes to kept. Returns the length read, or -1 if
// the end did not come within the deadline.
static long long readToEnd(int fd, long long deadline, char *kept, size_t keep, int *uniform)
{
    char chunk[65536];
    long long total = 0;
    for (;;)
    {
        const long long left = deadline - milliseconds();
        struct pollfd readable = {fd, POLLIN, 0};
        if (left <= 0 || poll(&readable, 1, (int)left) <= 0)
            return -1;
        const ssize_t count = read(fd, chunk, sizeof chunk);
        if (count == 0)
            return total;
        if (count < 0)
            return -1;
        for (ssize_t i = 0; i < count; ++i)
        {
            if ((size_t)total + (size_t)i < keep)
                kept[total + i] = chunk[i];
            if (uniform && chunk[i] != (total + i ? kept[0] : chunk[0]))
                *uniform = 0;
        }
        total += count;
    }
}

static int receive(const char *number, const char *mime, int *readEnd)
{
    const int index = atoi(number);
    if (index < 0 || index >= app.offerCount || !app.offers[index] || !mime)
        return -1;
    int fds[2];
    if (pipe2(fds, O_CLOEXEC) < 0)
        return -1;
    wl_data_offer_receive(app.offers[index], mime, fds[1]);
    close(fds[1]);
    wl_display_flush(app.display);
    *readEnd = fds[0];
    return index;
}

static void synced(void *data, struct wl_callback *callback, uint32_t serial)
{
    (void)data, (void)serial;
    wl_callback_destroy(callback);
    say("synced");
}
static const struct wl_callback_listener syncListener = {synced};

static void command(char *line)
{
    char *verb = strtok(line, " "), *number = strtok(NULL, " "), *mime = strtok(NULL, " ");
    int fd = -1, index;
    if (!verb)
        return;
    if (!strcmp(verb, "stall") && app.stallCount < maximumStalls && receive(number, mime, &fd) >= 0)
    {
        app.stalls[app.stallCount++] = fd;
        say("stalling %d", app.stallCount);
    }
    else if (!strcmp(verb, "sync"))
    {
        wl_callback_add_listener(wl_display_sync(app.display), &syncListener, NULL);
        wl_display_flush(app.display);
    }
    else if (!strcmp(verb, "close") && (index = receive(number, mime, &fd)) >= 0)
    {
        // Closed before the compositor can have written anything.
        close(fd);
        wl_display_roundtrip(app.display);
        say("closed %d", index);
    }
    else if (!strcmp(verb, "read") && (index = receive(number, mime, &fd)) >= 0)
    {
        char prefix[17] = {0};
        int uniform = 1;
        const long long length = readToEnd(fd, milliseconds() + 15000, prefix, 16, &uniform);
        close(fd);
        for (char *c = prefix; *c; ++c)
            if (*c == ' ' || *c == '\n' || *c == '\t')
                *c = '_';
        if (length < 0)
            say("data %d timeout", index);
        else
            say("data %d %lld %s %d", index, length, length ? prefix : "-", length ? uniform : 0);
    }
    else if (!strcmp(verb, "release"))
    {
        long long total = 0;
        const long long deadline = milliseconds() + 15000;
        for (int i = 0; i < app.stallCount; ++i)
        {
            char ignored[1];
            const long long length = readToEnd(app.stalls[i], deadline, ignored, 0, NULL);
            total += length > 0 ? length : 0;
            close(app.stalls[i]);
        }
        app.stallCount = 0;
        say("released %lld", total);
    }
    else if (!strcmp(verb, "drop"))
    {
        const int count = app.stallCount;
        for (int i = 0; i < app.stallCount; ++i)
            close(app.stalls[i]);
        app.stallCount = 0;
        say("dropped %d", count);
    }
    else if (!strcmp(verb, "quit"))
        app.running = 0;
    else
        say("refused %s offer=%s of %d mime=%s errno=%d", verb, number ? number : "-", app.offerCount,
            mime ? mime : "-", errno);
}

static void offerMime(void *data, struct wl_data_offer *offer, const char *mime)
{
    (void)data, (void)offer, (void)mime;
}
static void offerActions(void *data, struct wl_data_offer *offer, uint32_t actions)
{
    (void)data, (void)offer, (void)actions;
}
static void offerAction(void *data, struct wl_data_offer *offer, uint32_t action)
{
    (void)data, (void)offer, (void)action;
}
static const struct wl_data_offer_listener offerListener = {offerMime, offerActions, offerAction};

static void deviceOffer(void *data, struct wl_data_device *device, struct wl_data_offer *offer)
{
    (void)data, (void)device;
    if (app.offerCount == maximumOffers)
    {
        wl_data_offer_destroy(offer);
        return;
    }
    app.offers[app.offerCount++] = offer;
    wl_data_offer_add_listener(offer, &offerListener, NULL);
}
static void deviceEnter(void *data, struct wl_data_device *device, uint32_t serial, struct wl_surface *surface,
                        wl_fixed_t x, wl_fixed_t y, struct wl_data_offer *offer)
{
    (void)data, (void)device, (void)serial, (void)surface, (void)x, (void)y, (void)offer;
}
static void deviceLeave(void *data, struct wl_data_device *device)
{
    (void)data, (void)device;
}
static void deviceMotion(void *data, struct wl_data_device *device, uint32_t time, wl_fixed_t x, wl_fixed_t y)
{
    (void)data, (void)device, (void)time, (void)x, (void)y;
}
static void deviceDrop(void *data, struct wl_data_device *device)
{
    (void)data, (void)device;
}
static void deviceSelection(void *data, struct wl_data_device *device, struct wl_data_offer *offer)
{
    (void)data, (void)device;
    for (int i = app.offerCount - 1; offer && i >= 0; --i)
        if (app.offers[i] == offer)
        {
            say("selection %d", i);
            return;
        }
    say("selection none");
}
static const struct wl_data_device_listener deviceListener = {deviceOffer, deviceEnter, deviceLeave,
                                                              deviceMotion, deviceDrop, deviceSelection};

static void keymap(void *data, struct wl_keyboard *keyboard, uint32_t format, int32_t fd, uint32_t size)
{
    (void)data, (void)keyboard, (void)format, (void)size;
    close(fd);
}
static void keyboardEnter(void *data, struct wl_keyboard *keyboard, uint32_t serial, struct wl_surface *surface,
                          struct wl_array *keys)
{
    (void)data, (void)keyboard, (void)serial, (void)surface, (void)keys;
    say("focus");
}
static void keyboardLeave(void *data, struct wl_keyboard *keyboard, uint32_t serial, struct wl_surface *surface)
{
    (void)data, (void)keyboard, (void)serial, (void)surface;
    say("unfocus");
}
static void key(void *data, struct wl_keyboard *keyboard, uint32_t serial, uint32_t time, uint32_t code,
                uint32_t state)
{
    (void)data, (void)keyboard, (void)serial, (void)time;
    say("key %u %u", code, state);
}
static void modifiers(void *data, struct wl_keyboard *keyboard, uint32_t serial, uint32_t depressed,
                      uint32_t latched, uint32_t locked, uint32_t group)
{
    (void)data, (void)keyboard, (void)serial, (void)depressed, (void)latched, (void)locked, (void)group;
}
static void repeatInfo(void *data, struct wl_keyboard *keyboard, int32_t rate, int32_t delay)
{
    (void)data, (void)keyboard, (void)rate, (void)delay;
}
static const struct wl_keyboard_listener keyboardListener = {keymap, keyboardEnter, keyboardLeave,
                                                             key,    modifiers,     repeatInfo};

static void capabilities(void *data, struct wl_seat *seat, uint32_t capabilities)
{
    (void)data;
    if ((capabilities & WL_SEAT_CAPABILITY_KEYBOARD) && !app.keyboard)
    {
        app.keyboard = wl_seat_get_keyboard(seat);
        wl_keyboard_add_listener(app.keyboard, &keyboardListener, NULL);
    }
}
static void seatName(void *data, struct wl_seat *seat, const char *name)
{
    (void)data, (void)seat, (void)name;
}
static const struct wl_seat_listener seatListener = {capabilities, seatName};

static void ping(void *data, struct xdg_wm_base *wm, uint32_t serial)
{
    (void)data;
    xdg_wm_base_pong(wm, serial);
}
static const struct xdg_wm_base_listener wmListener = {ping};

static void surfaceConfigure(void *data, struct xdg_surface *surface, uint32_t serial)
{
    (void)data;
    xdg_surface_ack_configure(surface, serial);
    if (!app.buffer)
        app.buffer = makeBuffer(240, 120);
    wl_surface_attach(app.surface, app.buffer, 0, 0);
    wl_surface_damage(app.surface, 0, 0, 240, 120);
    wl_surface_commit(app.surface);
    if (!app.ready)
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
    else if (!strcmp(interface, wl_seat_interface.name) && !app.seat)
        BIND(app.seat, wl_seat_interface, 5);
    else if (!strcmp(interface, wl_data_device_manager_interface.name))
        BIND(app.manager, wl_data_device_manager_interface, 3);
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
    if (!app.compositor || !app.shm || !app.wm || !app.seat || !app.manager)
    {
        say("missing-globals");
        return 2;
    }
    xdg_wm_base_add_listener(app.wm, &wmListener, NULL);
    wl_seat_add_listener(app.seat, &seatListener, NULL);
    app.device = wl_data_device_manager_get_data_device(app.manager, app.seat);
    wl_data_device_add_listener(app.device, &deviceListener, NULL);
    app.surface = wl_compositor_create_surface(app.compositor);
    app.xdgSurface = xdg_wm_base_get_xdg_surface(app.wm, app.surface);
    xdg_surface_add_listener(app.xdgSurface, &surfaceListener, NULL);
    app.toplevel = xdg_surface_get_toplevel(app.xdgSurface);
    xdg_toplevel_add_listener(app.toplevel, &toplevelListener, NULL);
    xdg_toplevel_set_title(app.toplevel, "Paste Probe");
    xdg_toplevel_set_app_id(app.toplevel, "org.frankenstein.PasteProbe");
    wl_surface_commit(app.surface);

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
        const struct wl_interface *interface = NULL;
        uint32_t id = 0;
        const uint32_t code = wl_display_get_protocol_error(app.display, &interface, &id);
        say("protocol-error %s %u", interface ? interface->name : "none", code);
        return 4;
    }
    wl_display_disconnect(app.display);
    return 0;
}
