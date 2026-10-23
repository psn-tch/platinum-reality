// Qualification only: pause after registry advertisement, then bind the saved
// output names after the compositor has removed one. This deterministically
// exercises requests already in flight at global removal, without a GUI toolkit.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wayland-client.h>

static uint32_t names[32];
static int count, completed;
static void global(void *data, struct wl_registry *registry, uint32_t name, const char *interface, uint32_t version)
{
    (void)data; (void)registry;
    if (!strcmp(interface, "wl_output") && version >= 2 && count < 32)
        names[count++] = name;
}
static void removed(void *data, struct wl_registry *registry, uint32_t name)
{
    (void)data; (void)registry; (void)name;
    // Keep the earlier snapshot: bind requests may already be queued by a client.
}
static const struct wl_registry_listener registry_listener = {global, removed};
static void geometry(void *data, struct wl_output *output, int32_t x, int32_t y, int32_t w, int32_t h,
                     int32_t subpixel, const char *make, const char *model, int32_t transform)
{
    (void)data; (void)output; (void)x; (void)y; (void)w; (void)h;
    (void)subpixel; (void)make; (void)model; (void)transform;
}
static void mode(void *data, struct wl_output *output, uint32_t flags, int32_t w, int32_t h, int32_t refresh)
{
    (void)data; (void)output; (void)flags; (void)w; (void)h; (void)refresh;
}
static void done(void *data, struct wl_output *output) { (void)data; (void)output; ++completed; }
static void scale(void *data, struct wl_output *output, int32_t factor) { (void)data; (void)output; (void)factor; }
static const struct wl_output_listener output_listener = {
    .geometry = geometry, .mode = mode, .done = done, .scale = scale
};
int main(void)
{
    if (!getenv("FRANKENSTEIN_NATIVE_TEST") || strcmp(getenv("FRANKENSTEIN_NATIVE_TEST"), "1"))
        return 2;
    struct wl_display *display = wl_display_connect(NULL);
    if (!display) return 3;
    struct wl_registry *registry = wl_display_get_registry(display);
    wl_registry_add_listener(registry, &registry_listener, NULL);
    if (wl_display_roundtrip(display) < 0) return 4;
    printf("announced %d\n", count); fflush(stdout);
    char command[32];
    if (!fgets(command, sizeof command, stdin) || strcmp(command, "bind\n")) return 5;
    for (int i = 0; i < count; ++i)
    {
        struct wl_output *output = wl_registry_bind(registry, names[i], &wl_output_interface, 2);
        wl_output_add_listener(output, &output_listener, NULL);
    }
    if (wl_display_roundtrip(display) < 0) return 6;
    printf("bound %d done %d\n", count, completed); fflush(stdout);
    wl_display_disconnect(display);
    return completed == count ? 0 : 7;
}
