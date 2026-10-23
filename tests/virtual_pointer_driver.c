// Drives the HOST compositor's pointer (the owned headless Sway) through wlr-virtual-pointer, so
// the desktop receives real wl_pointer and relative-pointer events, with the host applying its
// own pointer constraints. Never run against a user's live session. One command per line:
//   abs X Y W H      absolute position within a W x H layout     -> ok
//   rel DX DY        relative motion                             -> ok
//   button CODE 0|1  release or press a Linux input button code  -> ok
//   finger v|h VALUE two-finger touchpad scrolling by VALUE      -> ok
//   stop v|h         the fingers lift from the touchpad          -> ok
//   wheel v|h N      N notches of a mouse wheel                  -> ok
//   quit
#define _GNU_SOURCE
#include "wlr-virtual-pointer-client.h"
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <wayland-client.h>

static struct wl_seat *seat;
static struct zwlr_virtual_pointer_manager_v1 *manager;

static void global(void *data, struct wl_registry *registry, uint32_t name, const char *interface,
                   uint32_t version)
{
    (void)data, (void)version;
    if (!strcmp(interface, wl_seat_interface.name) && !seat)
        seat = wl_registry_bind(registry, name, &wl_seat_interface, 1);
    else if (!strcmp(interface, zwlr_virtual_pointer_manager_v1_interface.name))
        manager = wl_registry_bind(registry, name, &zwlr_virtual_pointer_manager_v1_interface, 1);
}
static void globalRemove(void *data, struct wl_registry *registry, uint32_t name)
{
    (void)data, (void)registry, (void)name;
}
static const struct wl_registry_listener registryListener = {global, globalRemove};

static uint32_t now(void)
{
    struct timespec time;
    clock_gettime(CLOCK_MONOTONIC, &time);
    return (uint32_t)(time.tv_sec * 1000 + time.tv_nsec / 1000000);
}

int main(void)
{
    struct wl_display *display = wl_display_connect(NULL);
    if (!display)
    {
        puts("no-display");
        return 2;
    }
    struct wl_registry *registry = wl_display_get_registry(display);
    wl_registry_add_listener(registry, &registryListener, NULL);
    wl_display_roundtrip(display);
    if (!seat || !manager)
    {
        puts("unsupported");
        return 2;
    }
    struct zwlr_virtual_pointer_v1 *pointer = zwlr_virtual_pointer_manager_v1_create_virtual_pointer(manager, seat);
    wl_display_roundtrip(display);
    puts("driver-ready");
    fflush(stdout);
    char line[256];
    while (fgets(line, sizeof line, stdin))
    {
        unsigned x, y, width, height, code, state;
        double dx, dy, amount;
        char direction;
        int notches;
        if (sscanf(line, "abs %u %u %u %u", &x, &y, &width, &height) == 4)
            zwlr_virtual_pointer_v1_motion_absolute(pointer, now(), x, y, width, height);
        else if (sscanf(line, "rel %lf %lf", &dx, &dy) == 2)
            zwlr_virtual_pointer_v1_motion(pointer, now(), wl_fixed_from_double(dx), wl_fixed_from_double(dy));
        else if (sscanf(line, "button %u %u", &code, &state) == 2)
            zwlr_virtual_pointer_v1_button(pointer, now(), code, state);
        else if (sscanf(line, "finger %c %lf", &direction, &amount) == 2 && (direction == 'v' || direction == 'h'))
        {
            const uint32_t axis = direction == 'v' ? WL_POINTER_AXIS_VERTICAL_SCROLL : WL_POINTER_AXIS_HORIZONTAL_SCROLL;
            zwlr_virtual_pointer_v1_axis_source(pointer, WL_POINTER_AXIS_SOURCE_FINGER);
            zwlr_virtual_pointer_v1_axis(pointer, now(), axis, wl_fixed_from_double(amount));
        }
        else if (sscanf(line, "stop %c", &direction) == 1 && (direction == 'v' || direction == 'h'))
        {
            zwlr_virtual_pointer_v1_axis_source(pointer, WL_POINTER_AXIS_SOURCE_FINGER);
            zwlr_virtual_pointer_v1_axis_stop(pointer, now(), direction == 'v' ? WL_POINTER_AXIS_VERTICAL_SCROLL
                                                                              : WL_POINTER_AXIS_HORIZONTAL_SCROLL);
        }
        else if (sscanf(line, "wheel %c %d", &direction, &notches) == 2 && (direction == 'v' || direction == 'h'))
        {
            const uint32_t axis = direction == 'v' ? WL_POINTER_AXIS_VERTICAL_SCROLL : WL_POINTER_AXIS_HORIZONTAL_SCROLL;
            zwlr_virtual_pointer_v1_axis_source(pointer, WL_POINTER_AXIS_SOURCE_WHEEL);
            zwlr_virtual_pointer_v1_axis_discrete(pointer, now(), axis, wl_fixed_from_int(15 * notches), notches);
        }
        else if (!strncmp(line, "quit", 4))
            break;
        else
        {
            puts("bad-command");
            fflush(stdout);
            continue;
        }
        zwlr_virtual_pointer_v1_frame(pointer);
        if (wl_display_roundtrip(display) < 0)
        {
            puts("disconnected");
            return 3;
        }
        puts("ok");
        fflush(stdout);
    }
    zwlr_virtual_pointer_v1_destroy(pointer);
    wl_display_roundtrip(display);
    wl_display_disconnect(display);
    return 0;
}
