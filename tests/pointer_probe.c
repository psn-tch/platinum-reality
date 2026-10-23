// A game-like raw Wayland client for pointer-capture qualification. It reads one command per
// line on stdin and prints one event per line on stdout:
//   relative                          bind a relative pointer          -> relative-ready
//   lock|confine oneshot|persistent [x y w h]                          -> requested
//   region x y w h                    set the constraint region        -> region-set
//   drop                              destroy the constraint           -> dropped
//   duplicate                         a second lock on the same surface (a protocol error)
//   cursor hide|show                  no cursor, or a 16x16 cursor with hotspot 3,4 -> cursor-set
//   idle on|off                       an idle inhibitor on the surface -> idle-set
//   fullscreen on|off                 full screen with no output named -> fullscreen-set
//   quit
// With --resizable the window has no fixed size and takes each configured size.
// Events: seat VERSION, ready WxH, configure W H [fullscreen], enter X Y, leave, motion X Y, button CODE STATE,
// relative DX DY UX UY, locked, unlocked, confined, unconfined, protocol-error INTERFACE CODE,
// one line per scrolling frame: scroll wheel|finger|continuous|tilt|unknown DX DY [discrete N M] [stop]
// (axis events without a frame on an old seat: scroll none DX DY), and touch-down ID X Y,
// touch-motion ID X Y, touch-up ID, touch-cancel.
#define _GNU_SOURCE
#include "idle-inhibit-client.h"
#include "pointer-constraints-client.h"
#include "relative-pointer-client.h"
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

static struct
{
    struct wl_display *display;
    struct wl_compositor *compositor;
    struct wl_shm *shm;
    struct xdg_wm_base *wm;
    struct wl_seat *seat;
    uint32_t seatVersion;
    struct wl_pointer *pointer;
    struct wl_touch *touch;
    struct wl_touch *extraTouch;
    struct zwp_relative_pointer_manager_v1 *relativeManager;
    struct zwp_pointer_constraints_v1 *constraints;
    struct zwp_idle_inhibit_manager_v1 *idle;
    struct zwp_idle_inhibitor_v1 *inhibitor;
    struct zwp_relative_pointer_v1 *relative;
    struct zwp_locked_pointer_v1 *locked;
    struct zwp_confined_pointer_v1 *confined;
    struct wl_surface *surface;
    struct xdg_surface *xdgSurface;
    struct xdg_toplevel *toplevel;
    struct wl_buffer *buffer;
    struct wl_surface *cursor;
    uint32_t enterSerial;
    int width, height, ready, running;
} app = {.width = 320, .height = 240, .running = 1};

static void say(const char *format, ...)
{
    va_list arguments;
    va_start(arguments, format);
    vprintf(format, arguments);
    va_end(arguments);
    putchar('\n');
    fflush(stdout);
}

static struct wl_buffer *makeBuffer(int width, int height, uint32_t color)
{
    const int stride = width * 4, size = stride * height;
    const int fd = memfd_create("pointer-probe", MFD_CLOEXEC);
    if (fd < 0 || ftruncate(fd, size) < 0)
        return NULL;
    uint32_t *pixels = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (pixels == MAP_FAILED)
        return NULL;
    for (int i = 0; i < width * height; ++i)
        pixels[i] = color;
    munmap(pixels, size);
    struct wl_shm_pool *pool = wl_shm_create_pool(app.shm, fd, size);
    struct wl_buffer *buffer =
        wl_shm_pool_create_buffer(pool, 0, width, height, stride, WL_SHM_FORMAT_ARGB8888);
    wl_shm_pool_destroy(pool);
    close(fd);
    return buffer;
}

static void pointerEnter(void *data, struct wl_pointer *pointer, uint32_t serial,
                         struct wl_surface *surface, wl_fixed_t x, wl_fixed_t y)
{
    (void)data, (void)pointer, (void)surface;
    app.enterSerial = serial;
    say("enter %.1f %.1f", wl_fixed_to_double(x), wl_fixed_to_double(y));
}
static void pointerLeave(void *data, struct wl_pointer *pointer, uint32_t serial, struct wl_surface *surface)
{
    (void)data, (void)pointer, (void)serial, (void)surface;
    say("leave");
}
static void pointerMotion(void *data, struct wl_pointer *pointer, uint32_t time, wl_fixed_t x, wl_fixed_t y)
{
    (void)data, (void)pointer, (void)time;
    say("motion %.1f %.1f", wl_fixed_to_double(x), wl_fixed_to_double(y));
}
static void pointerButton(void *data, struct wl_pointer *pointer, uint32_t serial, uint32_t time,
                          uint32_t button, uint32_t state)
{
    (void)data, (void)pointer, (void)serial, (void)time;
    say("button %u %u", button, state);
}
// Scrolling is gathered until the frame that ends it, as applications do.
static struct
{
    int scrolled, stopped, discrete, source;
    double x, y;
    int discreteX, discreteY;
} scroll = {.source = -1};

static void pointerAxis(void *data, struct wl_pointer *pointer, uint32_t time, uint32_t axis, wl_fixed_t value)
{
    (void)data, (void)pointer, (void)time;
    const double amount = wl_fixed_to_double(value);
    if (app.seatVersion < WL_POINTER_FRAME_SINCE_VERSION)
    {
        say("scroll none %.2f %.2f", axis == WL_POINTER_AXIS_HORIZONTAL_SCROLL ? amount : 0.0,
            axis == WL_POINTER_AXIS_VERTICAL_SCROLL ? amount : 0.0);
        return;
    }
    scroll.scrolled = 1;
    if (axis == WL_POINTER_AXIS_VERTICAL_SCROLL)
        scroll.y += amount;
    else
        scroll.x += amount;
}
static void pointerFrame(void *data, struct wl_pointer *pointer)
{
    (void)data, (void)pointer;
    static const char *const sources[] = {"wheel", "finger", "continuous", "tilt"};
    if (scroll.scrolled || scroll.stopped)
    {
        char discrete[64] = "";
        if (scroll.discrete)
            snprintf(discrete, sizeof discrete, " discrete %d %d", scroll.discreteX, scroll.discreteY);
        say("scroll %s %.2f %.2f%s%s", scroll.source >= 0 && scroll.source < 4 ? sources[scroll.source] : "unknown",
            scroll.x, scroll.y, discrete, scroll.stopped ? " stop" : "");
    }
    memset(&scroll, 0, sizeof scroll);
    scroll.source = -1;
}
static void pointerAxisSource(void *data, struct wl_pointer *pointer, uint32_t source)
{
    (void)data, (void)pointer;
    scroll.source = (int)source;
}
static void pointerAxisStop(void *data, struct wl_pointer *pointer, uint32_t time, uint32_t axis)
{
    (void)data, (void)pointer, (void)time, (void)axis;
    scroll.stopped = 1;
}
static void pointerAxisDiscrete(void *data, struct wl_pointer *pointer, uint32_t axis, int32_t discrete)
{
    (void)data, (void)pointer;
    scroll.discrete = 1;
    if (axis == WL_POINTER_AXIS_VERTICAL_SCROLL)
        scroll.discreteY += discrete;
    else
        scroll.discreteX += discrete;
}
// The seat is bound at version 5, so later wl_pointer events are never sent.
static const struct wl_pointer_listener pointerListener = {
    .enter = pointerEnter, .leave = pointerLeave, .motion = pointerMotion, .button = pointerButton,
    .axis = pointerAxis, .frame = pointerFrame, .axis_source = pointerAxisSource,
    .axis_stop = pointerAxisStop, .axis_discrete = pointerAxisDiscrete};

static void touchDown(void *data, struct wl_touch *touch, uint32_t serial, uint32_t time,
                      struct wl_surface *surface, int32_t id, wl_fixed_t x, wl_fixed_t y)
{
    (void)data, (void)touch, (void)serial, (void)time, (void)surface;
    say("%stouch-down %d %.1f %.1f", data ? (const char *)data : "", id, wl_fixed_to_double(x), wl_fixed_to_double(y));
}
static void touchUp(void *data, struct wl_touch *touch, uint32_t serial, uint32_t time, int32_t id)
{
    (void)data, (void)touch, (void)serial, (void)time;
    say("%stouch-up %d", data ? (const char *)data : "", id);
}
static void touchMotion(void *data, struct wl_touch *touch, uint32_t time, int32_t id, wl_fixed_t x, wl_fixed_t y)
{
    (void)data, (void)touch, (void)time;
    say("%stouch-motion %d %.1f %.1f", data ? (const char *)data : "", id, wl_fixed_to_double(x), wl_fixed_to_double(y));
}
static void touchFrame(void *data, struct wl_touch *touch)
{
    (void)data, (void)touch;
}
static void touchCancel(void *data, struct wl_touch *touch)
{
    (void)data, (void)touch;
    say("%stouch-cancel", data ? (const char *)data : "");
}
static void touchShape(void *data, struct wl_touch *touch, int32_t id, wl_fixed_t major, wl_fixed_t minor)
{
    (void)data, (void)touch, (void)id, (void)major, (void)minor;
}
static void touchOrientation(void *data, struct wl_touch *touch, int32_t id, wl_fixed_t orientation)
{
    (void)data, (void)touch, (void)id, (void)orientation;
}
static const struct wl_touch_listener touchListener = {
    .down = touchDown, .up = touchUp, .motion = touchMotion, .frame = touchFrame, .cancel = touchCancel,
    .shape = touchShape, .orientation = touchOrientation};

static void seatCapabilities(void *data, struct wl_seat *seat, uint32_t capabilities)
{
    (void)data;
    if ((capabilities & WL_SEAT_CAPABILITY_POINTER) && !app.pointer)
    {
        app.pointer = wl_seat_get_pointer(seat);
        wl_pointer_add_listener(app.pointer, &pointerListener, NULL);
    }
    if ((capabilities & WL_SEAT_CAPABILITY_TOUCH) && !app.touch)
    {
        app.touch = wl_seat_get_touch(seat);
        wl_touch_add_listener(app.touch, &touchListener, NULL);
    }
}
static void seatName(void *data, struct wl_seat *seat, const char *name)
{
    (void)data, (void)seat, (void)name;
}
static const struct wl_seat_listener seatListener = {seatCapabilities, seatName};

static void relativeMotion(void *data, struct zwp_relative_pointer_v1 *relative, uint32_t high,
                           uint32_t low, wl_fixed_t dx, wl_fixed_t dy, wl_fixed_t ux, wl_fixed_t uy)
{
    (void)data, (void)relative, (void)high, (void)low;
    say("relative %.2f %.2f %.2f %.2f", wl_fixed_to_double(dx), wl_fixed_to_double(dy),
        wl_fixed_to_double(ux), wl_fixed_to_double(uy));
}
static const struct zwp_relative_pointer_v1_listener relativeListener = {relativeMotion};

static void locked(void *data, struct zwp_locked_pointer_v1 *pointer)
{
    (void)data, (void)pointer;
    say("locked");
}
static void unlocked(void *data, struct zwp_locked_pointer_v1 *pointer)
{
    (void)data, (void)pointer;
    say("unlocked");
}
static const struct zwp_locked_pointer_v1_listener lockedListener = {locked, unlocked};
static void confined(void *data, struct zwp_confined_pointer_v1 *pointer)
{
    (void)data, (void)pointer;
    say("confined");
}
static void unconfined(void *data, struct zwp_confined_pointer_v1 *pointer)
{
    (void)data, (void)pointer;
    say("unconfined");
}
static const struct zwp_confined_pointer_v1_listener confinedListener = {confined, unconfined};

static void ping(void *data, struct xdg_wm_base *wm, uint32_t serial)
{
    (void)data;
    xdg_wm_base_pong(wm, serial);
}
static const struct xdg_wm_base_listener wmListener = {ping};
// --resizable: no fixed size, and full screen (or any configured size) gets a buffer of that size.
static int resizable, pendingWidth, pendingHeight;
static void surfaceConfigure(void *data, struct xdg_surface *surface, uint32_t serial)
{
    (void)data;
    xdg_surface_ack_configure(surface, serial);
    if (resizable && pendingWidth > 0 && pendingHeight > 0 &&
        (pendingWidth != app.width || pendingHeight != app.height))
    {
        if (app.buffer)
            wl_buffer_destroy(app.buffer);
        app.buffer = NULL;
        app.width = pendingWidth;
        app.height = pendingHeight;
    }
    if (!app.buffer)
        app.buffer = makeBuffer(app.width, app.height, 0xff3060a0);
    wl_surface_attach(app.surface, app.buffer, 0, 0);
    wl_surface_damage(app.surface, 0, 0, app.width, app.height);
    wl_surface_commit(app.surface);
    if (!app.ready)
    {
        app.ready = 1;
        say("ready %dx%d", app.width, app.height);
    }
}
static const struct xdg_surface_listener surfaceListener = {surfaceConfigure};
static void toplevelConfigure(void *data, struct xdg_toplevel *toplevel, int32_t width, int32_t height,
                              struct wl_array *states)
{
    (void)data, (void)toplevel;
    int fullscreen = 0;
    const uint32_t *state;
    wl_array_for_each(state, states) fullscreen |= *state == XDG_TOPLEVEL_STATE_FULLSCREEN;
    pendingWidth = width;
    pendingHeight = height;
    say("configure %d %d%s", width, height, fullscreen ? " fullscreen" : "");
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
    if (!strcmp(interface, wl_compositor_interface.name))
        app.compositor = wl_registry_bind(registry, name, &wl_compositor_interface, 4);
    else if (!strcmp(interface, wl_shm_interface.name))
        app.shm = wl_registry_bind(registry, name, &wl_shm_interface, 1);
    else if (!strcmp(interface, xdg_wm_base_interface.name))
        app.wm = wl_registry_bind(registry, name, &xdg_wm_base_interface, 1);
    else if (!strcmp(interface, wl_seat_interface.name) && !app.seat)
    {
        app.seatVersion = version < 5 ? version : 5;
        app.seat = wl_registry_bind(registry, name, &wl_seat_interface, app.seatVersion);
    }
    else if (!strcmp(interface, zwp_relative_pointer_manager_v1_interface.name))
        app.relativeManager = wl_registry_bind(registry, name, &zwp_relative_pointer_manager_v1_interface, 1);
    else if (!strcmp(interface, zwp_pointer_constraints_v1_interface.name))
        app.constraints = wl_registry_bind(registry, name, &zwp_pointer_constraints_v1_interface, 1);
    else if (!strcmp(interface, zwp_idle_inhibit_manager_v1_interface.name))
        app.idle = wl_registry_bind(registry, name, &zwp_idle_inhibit_manager_v1_interface, 1);
}
static void globalRemove(void *data, struct wl_registry *registry, uint32_t name)
{
    (void)data, (void)registry, (void)name;
}
static const struct wl_registry_listener registryListener = {global, globalRemove};

static struct wl_region *parseRegion(const char *text)
{
    int x, y, w, h;
    if (!text || sscanf(text, "%d %d %d %d", &x, &y, &w, &h) != 4)
        return NULL;
    struct wl_region *region = wl_compositor_create_region(app.compositor);
    wl_region_add(region, x, y, w, h);
    return region;
}

static void constrain(int lock, const char *arguments)
{
    char lifetime[16] = "";
    int consumed = 0;
    if (sscanf(arguments, "%15s %n", lifetime, &consumed) < 1)
    {
        say("bad-command");
        return;
    }
    const uint32_t value = !strcmp(lifetime, "persistent") ? ZWP_POINTER_CONSTRAINTS_V1_LIFETIME_PERSISTENT
                                                            : ZWP_POINTER_CONSTRAINTS_V1_LIFETIME_ONESHOT;
    struct wl_region *region = parseRegion(arguments + consumed);
    if (lock)
    {
        app.locked = zwp_pointer_constraints_v1_lock_pointer(app.constraints, app.surface, app.pointer,
                                                             region, value);
        zwp_locked_pointer_v1_add_listener(app.locked, &lockedListener, NULL);
    }
    else
    {
        app.confined = zwp_pointer_constraints_v1_confine_pointer(app.constraints, app.surface, app.pointer,
                                                                  region, value);
        zwp_confined_pointer_v1_add_listener(app.confined, &confinedListener, NULL);
    }
    if (region)
        wl_region_destroy(region);
    say("requested");
}

static void recoveredFrame(void *data, struct wl_callback *callback, uint32_t time)
{
    (void)data; (void)time;
    wl_callback_destroy(callback);
    app.buffer = makeBuffer(app.width, app.height, 0xff207040);
    wl_surface_attach(app.surface, app.buffer, 0, 0);
    wl_surface_damage(app.surface, 0, 0, app.width, app.height);
    wl_surface_commit(app.surface);
    say("buffer-recovered");
}
static const struct wl_callback_listener recoveryListener = {recoveredFrame};

static void command(const char *line)
{
    if (!strcmp(line, "quit"))
        app.running = 0;
    else if (!strcmp(line, "touch-extra") && !app.extraTouch)
    {
        app.extraTouch = wl_seat_get_touch(app.seat);
        wl_touch_add_listener(app.extraTouch, &touchListener, "extra-");
        if (wl_display_roundtrip(app.display) < 0) { app.running = 0; return; }
        say("extra-touch-ready");
    }
    else if (!strcmp(line, "touch-release-extra") && app.extraTouch)
    {
        wl_touch_release(app.extraTouch);
        app.extraTouch = NULL;
        if (wl_display_roundtrip(app.display) < 0) { app.running = 0; return; }
        say("extra-touch-released");
    }
    else if (!strcmp(line, "replace-buffer"))
    {
        struct wl_buffer *temporary = makeBuffer(app.width, app.height, 0xff806020);
        wl_surface_attach(app.surface, temporary, 0, 0);
        wl_surface_damage(app.surface, 0, 0, app.width, app.height);
        wl_surface_commit(app.surface);
        wl_buffer_destroy(temporary);
        if (app.buffer) wl_buffer_destroy(app.buffer);
        app.buffer = NULL;
        struct wl_callback *callback = wl_surface_frame(app.surface);
        wl_callback_add_listener(callback, &recoveryListener, NULL);
        wl_surface_commit(app.surface); // State only, without attach(NULL).
        say("replacement-requested");
    }
    else if (!strcmp(line, "unmap"))
    {
        wl_surface_attach(app.surface, NULL, 0, 0);
        wl_surface_commit(app.surface);
        say("unmapped");
    }
    else if (!strcmp(line, "relative"))
    {
        if (!app.relative)
        {
            app.relative = zwp_relative_pointer_manager_v1_get_relative_pointer(app.relativeManager, app.pointer);
            zwp_relative_pointer_v1_add_listener(app.relative, &relativeListener, NULL);
        }
        say("relative-ready");
    }
    else if (!strncmp(line, "lock ", 5))
        constrain(1, line + 5);
    else if (!strncmp(line, "confine ", 8))
        constrain(0, line + 8);
    else if (!strcmp(line, "duplicate"))
    {
        struct zwp_locked_pointer_v1 *second = zwp_pointer_constraints_v1_lock_pointer(
            app.constraints, app.surface, app.pointer, NULL, ZWP_POINTER_CONSTRAINTS_V1_LIFETIME_ONESHOT);
        (void)second;
        say("requested");
    }
    else if (!strncmp(line, "region ", 7))
    {
        struct wl_region *region = parseRegion(line + 7);
        if (app.locked)
            zwp_locked_pointer_v1_set_region(app.locked, region);
        else if (app.confined)
            zwp_confined_pointer_v1_set_region(app.confined, region);
        if (region)
            wl_region_destroy(region);
        wl_surface_commit(app.surface);
        say("region-set");
    }
    else if (!strcmp(line, "idle on") && app.idle)
    {
        if (!app.inhibitor)
            app.inhibitor = zwp_idle_inhibit_manager_v1_create_inhibitor(app.idle, app.surface);
        say("idle-set");
    }
    else if (!strcmp(line, "idle off"))
    {
        if (app.inhibitor)
            zwp_idle_inhibitor_v1_destroy(app.inhibitor);
        app.inhibitor = NULL;
        say("idle-set");
    }
    else if (!strcmp(line, "cursor hide"))
    {
        wl_pointer_set_cursor(app.pointer, app.enterSerial, NULL, 0, 0);
        say("cursor-set");
    }
    else if (!strcmp(line, "fullscreen on") || !strcmp(line, "fullscreen off"))
    {
        // No output named, as GTK, browsers' F11 and video players ask: the compositor chooses.
        if (!strcmp(line, "fullscreen on"))
            xdg_toplevel_set_fullscreen(app.toplevel, NULL);
        else
            xdg_toplevel_unset_fullscreen(app.toplevel);
        wl_surface_commit(app.surface);
        say("fullscreen-set");
    }
    else if (!strcmp(line, "cursor show"))
    {
        if (!app.cursor)
        {
            app.cursor = wl_compositor_create_surface(app.compositor);
            wl_surface_attach(app.cursor, makeBuffer(16, 16, 0xffff0000), 0, 0);
            wl_surface_damage(app.cursor, 0, 0, 16, 16);
            wl_surface_commit(app.cursor);
        }
        wl_pointer_set_cursor(app.pointer, app.enterSerial, app.cursor, 3, 4);
        say("cursor-set");
    }
    else if (!strcmp(line, "cursor green"))
    {
        if (!app.cursor)
        {
            say("cursor-not-set");
            return;
        }
        // Test a cursor buffer update without a new set_cursor request. Like the original
        // red cursor buffer, this small test buffer lives until the disposable client exits.
        wl_surface_attach(app.cursor, makeBuffer(16, 16, 0xff00ff00), 0, 0);
        wl_surface_damage(app.cursor, 0, 0, 16, 16);
        wl_surface_commit(app.cursor);
        say("cursor-updated");
    }
    else if (!strcmp(line, "drop"))
    {
        if (app.locked)
            zwp_locked_pointer_v1_destroy(app.locked);
        if (app.confined)
            zwp_confined_pointer_v1_destroy(app.confined);
        app.locked = NULL;
        app.confined = NULL;
        say("dropped");
    }
    else
        say("bad-command");
}

int main(int argc, char **argv)
{
    resizable = argc > 1 && !strcmp(argv[1], "--resizable");
    app.display = wl_display_connect(NULL);
    if (!app.display)
    {
        say("no-display");
        return 2;
    }
    struct wl_registry *registry = wl_display_get_registry(app.display);
    wl_registry_add_listener(registry, &registryListener, NULL);
    wl_display_roundtrip(app.display);
    say("globals relative=%d constraints=%d idle=%d", app.relativeManager != NULL, app.constraints != NULL,
        app.idle != NULL);
    if (!app.compositor || !app.shm || !app.wm || !app.seat)
        return 2;
    say("seat %u", app.seatVersion);
    xdg_wm_base_add_listener(app.wm, &wmListener, NULL);
    wl_seat_add_listener(app.seat, &seatListener, NULL);
    app.surface = wl_compositor_create_surface(app.compositor);
    app.xdgSurface = xdg_wm_base_get_xdg_surface(app.wm, app.surface);
    xdg_surface_add_listener(app.xdgSurface, &surfaceListener, NULL);
    app.toplevel = xdg_surface_get_toplevel(app.xdgSurface);
    xdg_toplevel_add_listener(app.toplevel, &toplevelListener, NULL);
    xdg_toplevel_set_title(app.toplevel, "Pointer Probe");
    xdg_toplevel_set_app_id(app.toplevel, "org.frankenstein.PointerProbe");
    if (!resizable)
    {
        xdg_toplevel_set_min_size(app.toplevel, app.width, app.height);
        xdg_toplevel_set_max_size(app.toplevel, app.width, app.height);
    }
    wl_surface_commit(app.surface);

    char input[4096];
    size_t pending = 0;
    int failed = 0;
    while (app.running)
    {
        while (wl_display_prepare_read(app.display) != 0)
            if (wl_display_dispatch_pending(app.display) < 0)
            {
                failed = 1;
                break;
            }
        if (failed)
            break;
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
            while ((end = strchr(start, '\n')))
            {
                *end = '\0';
                command(start);
                start = end + 1;
            }
            pending = strlen(start);
            memmove(input, start, pending);
        }
    }
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
