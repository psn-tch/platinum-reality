// Types on the HOST compositor's seat (an owned headless Sway) through virtual-keyboard, so a
// lock screen receives real wl_keyboard input. Never run against a user's live session.
// One command per line:
//   type TEXT     lowercase letters, digits, space and . , - typed in order  -> ok
//   key NAME      return | escape | backspace                                 -> ok
//   quit
#define _GNU_SOURCE
#include "virtual-keyboard-client.h"
#include <linux/input-event-codes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <time.h>
#include <unistd.h>
#include <wayland-client.h>
#include <xkbcommon/xkbcommon.h>

static struct wl_seat *seat;
static struct zwp_virtual_keyboard_manager_v1 *manager;

static void global(void *data, struct wl_registry *registry, uint32_t name, const char *interface,
                   uint32_t version)
{
    (void)data, (void)version;
    if (!strcmp(interface, wl_seat_interface.name) && !seat)
        seat = wl_registry_bind(registry, name, &wl_seat_interface, 1);
    else if (!strcmp(interface, zwp_virtual_keyboard_manager_v1_interface.name))
        manager = wl_registry_bind(registry, name, &zwp_virtual_keyboard_manager_v1_interface, 1);
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

static int keycode(char c)
{
    static const char *const rows[] = {"1234567890-", "qwertyuiop", "asdfghjkl", "zxcvbnm,."};
    static const int starts[] = {KEY_1, KEY_Q, KEY_A, KEY_Z};
    if (c == ' ')
        return KEY_SPACE;
    for (int row = 0; row < 4; ++row)
        if (strchr(rows[row], c))
            return starts[row] + (int)(strchr(rows[row], c) - rows[row]);
    return -1;
}

static void press(struct zwp_virtual_keyboard_v1 *keyboard, int code)
{
    zwp_virtual_keyboard_v1_key(keyboard, now(), (uint32_t)code, WL_KEYBOARD_KEY_STATE_PRESSED);
    zwp_virtual_keyboard_v1_key(keyboard, now(), (uint32_t)code, WL_KEYBOARD_KEY_STATE_RELEASED);
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
    // A plain US keymap, compiled here and handed to the compositor.
    struct xkb_context *context = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
    const struct xkb_rule_names names = {.layout = "us"};
    struct xkb_keymap *keymap = context ? xkb_keymap_new_from_names(context, &names, XKB_KEYMAP_COMPILE_NO_FLAGS) : NULL;
    char *text = keymap ? xkb_keymap_get_as_string(keymap, XKB_KEYMAP_FORMAT_TEXT_V1) : NULL;
    if (!text)
    {
        puts("no-keymap");
        return 2;
    }
    const size_t size = strlen(text) + 1;
    const int fd = memfd_create("virtual-keyboard-driver", MFD_CLOEXEC);
    if (fd < 0 || write(fd, text, size) != (ssize_t)size)
        return 2;
    struct zwp_virtual_keyboard_v1 *keyboard = zwp_virtual_keyboard_manager_v1_create_virtual_keyboard(manager, seat);
    zwp_virtual_keyboard_v1_keymap(keyboard, WL_KEYBOARD_KEYMAP_FORMAT_XKB_V1, fd, (uint32_t)size);
    close(fd);
    wl_display_roundtrip(display);
    puts("driver-ready");
    fflush(stdout);
    char line[512];
    while (fgets(line, sizeof line, stdin))
    {
        line[strcspn(line, "\n")] = '\0';
        int ok = 1;
        if (!strncmp(line, "type ", 5))
        {
            for (const char *c = line + 5; *c && ok; ++c)
            {
                const int code = keycode(*c);
                if (code < 0)
                    ok = 0;
                else
                    press(keyboard, code);
            }
        }
        else if (!strcmp(line, "key return"))
            press(keyboard, KEY_ENTER);
        else if (!strcmp(line, "key escape"))
            press(keyboard, KEY_ESC);
        else if (!strcmp(line, "key backspace"))
            press(keyboard, KEY_BACKSPACE);
        else if (!strcmp(line, "quit"))
            break;
        else
            ok = 0;
        if (wl_display_roundtrip(display) < 0)
        {
            puts("disconnected");
            return 3;
        }
        puts(ok ? "ok" : "bad-command");
        fflush(stdout);
    }
    zwp_virtual_keyboard_v1_destroy(keyboard);
    wl_display_roundtrip(display);
    free(text);
    xkb_keymap_unref(keymap);
    xkb_context_unref(context);
    wl_display_disconnect(display);
    return 0;
}
