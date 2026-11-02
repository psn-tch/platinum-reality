// Owned ext-data-control client: deterministically holds a copy's send FD until
// instructed. Never connect to a user's clipboard. Commands (small ASCII data):
//   copy N WORD    create a fresh source and claim selection -> claimed N; send N
//   finish N       write its data/close its held FD           -> finished N COUNT
//   destroy N      destroy that source and close its FD       -> destroyed N
//   clear          explicitly clear the selection (NULL)      -> cleared
//   sync           round-trip after earlier requests          -> synced
//   quit
// Events: ready, cancelled N, selection present|none.
#define _GNU_SOURCE
#include "ext-data-control-client.h"
#include <errno.h>
#include <poll.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <wayland-client.h>

enum
{
    maximumSources = 32
};
static struct wl_display *display;
static struct wl_seat *seat;
static struct ext_data_control_manager_v1 *manager;
static struct ext_data_control_device_v1 *device;
static struct ext_data_control_offer_v1 *selection;
static int running = 1;
struct Source
{
    struct ext_data_control_source_v1 *proxy;
    char text[256];
    int fd, index;
};
static struct Source sources[maximumSources];
static void say(const char *format, ...)
{
    va_list args;
    va_start(args, format);
    vprintf(format, args);
    va_end(args);
    putchar('\n');
    fflush(stdout);
}
static void sourceSend(void *data, struct ext_data_control_source_v1 *source, const char *mime,
                       int32_t fd)
{
    (void)source;
    (void)mime;
    struct Source *item = data;
    if (item->fd >= 0)
        close(item->fd);
    item->fd = fd;
    say("send %d", item->index);
}
static void sourceCancelled(void *data, struct ext_data_control_source_v1 *source)
{
    (void)source;
    // Keep our FD until finish/destroy: tests may release it after cancellation
    // to prove that its old bytes cannot replace a later copy.
    say("cancelled %d", ((struct Source *)data)->index);
}
static const struct ext_data_control_source_v1_listener sourceListener = {sourceSend,
                                                                          sourceCancelled};
static void offerMime(void *data, struct ext_data_control_offer_v1 *offer, const char *mime)
{
    (void)data;
    (void)offer;
    (void)mime;
}
static const struct ext_data_control_offer_v1_listener offerListener = {offerMime};
static void dataOffer(void *data, struct ext_data_control_device_v1 *object,
                      struct ext_data_control_offer_v1 *offer)
{
    (void)data;
    (void)object;
    ext_data_control_offer_v1_add_listener(offer, &offerListener, NULL);
}
static void selected(void *data, struct ext_data_control_device_v1 *object,
                     struct ext_data_control_offer_v1 *offer)
{
    (void)data;
    (void)object;
    if (selection)
        ext_data_control_offer_v1_destroy(selection);
    selection = offer;
    say("selection %s", offer ? "present" : "none");
}
static void finished(void *data, struct ext_data_control_device_v1 *object)
{
    (void)data;
    (void)object;
    running = 0;
}
static void primary(void *data, struct ext_data_control_device_v1 *object,
                    struct ext_data_control_offer_v1 *offer)
{
    (void)data;
    (void)object;
    if (offer)
        ext_data_control_offer_v1_destroy(offer);
}
static const struct ext_data_control_device_v1_listener deviceListener = {dataOffer, selected,
                                                                          finished, primary};
static void global(void *data, struct wl_registry *registry, uint32_t name, const char *interface,
                   uint32_t version)
{
    (void)data;
    (void)version;
    if (!strcmp(interface, wl_seat_interface.name) && !seat)
        seat = wl_registry_bind(registry, name, &wl_seat_interface, 1);
    else if (!strcmp(interface, ext_data_control_manager_v1_interface.name))
        manager = wl_registry_bind(registry, name, &ext_data_control_manager_v1_interface, 1);
}
static void removed(void *data, struct wl_registry *registry, uint32_t name)
{
    (void)data;
    (void)registry;
    (void)name;
}
static const struct wl_registry_listener registryListener = {global, removed};
static void synced(void *data, struct wl_callback *callback, uint32_t serial)
{
    (void)data;
    (void)serial;
    wl_callback_destroy(callback);
    say("synced");
}
static const struct wl_callback_listener syncListener = {synced};
static void command(char *line)
{
    unsigned index;
    char text[256];
    if (sscanf(line, "copy %u %255s", &index, text) == 2 && index < maximumSources &&
        !sources[index].proxy)
    {
        struct Source *item = &sources[index];
        strcpy(item->text, text);
        item->proxy = ext_data_control_manager_v1_create_data_source(manager);
        ext_data_control_source_v1_add_listener(item->proxy, &sourceListener, item);
        ext_data_control_source_v1_offer(item->proxy, "text/plain");
        ext_data_control_device_v1_set_selection(device, item->proxy);
        say("claimed %u", index);
    }
    else if (sscanf(line, "finish %u", &index) == 1 && index < maximumSources)
    {
        struct Source *item = &sources[index];
        const ssize_t count = item->fd >= 0 ? write(item->fd, item->text, strlen(item->text)) : -1;
        if (item->fd >= 0)
            close(item->fd);
        item->fd = -1;
        say("finished %u %zd", index, count);
    }
    else if (sscanf(line, "destroy %u", &index) == 1 && index < maximumSources &&
             sources[index].proxy)
    {
        struct Source *item = &sources[index];
        ext_data_control_source_v1_destroy(item->proxy);
        item->proxy = NULL;
        if (item->fd >= 0)
            close(item->fd);
        item->fd = -1;
        say("destroyed %u", index);
    }
    else if (!strcmp(line, "clear"))
    {
        ext_data_control_device_v1_set_selection(device, NULL);
        say("cleared");
    }
    else if (!strcmp(line, "sync"))
        wl_callback_add_listener(wl_display_sync(display), &syncListener, NULL);
    else if (!strcmp(line, "quit"))
        running = 0;
    else
        say("refused");
}
int main(void)
{
    signal(SIGPIPE, SIG_IGN);
    for (int i = 0; i < maximumSources; ++i)
        sources[i] = (struct Source){.fd = -1, .index = i};
    display = wl_display_connect(NULL);
    if (!display)
        return 2;
    struct wl_registry *registry = wl_display_get_registry(display);
    wl_registry_add_listener(registry, &registryListener, NULL);
    if (wl_display_roundtrip(display) < 0 || !seat || !manager)
        return 2;
    device = ext_data_control_manager_v1_get_data_device(manager, seat);
    ext_data_control_device_v1_add_listener(device, &deviceListener, NULL);
    if (wl_display_roundtrip(display) < 0)
        return 2;
    say("ready");
    char input[1024];
    size_t pending = 0;
    while (running)
    {
        while (wl_display_prepare_read(display) != 0)
            if (wl_display_dispatch_pending(display) < 0)
                goto done;
        wl_display_flush(display);
        struct pollfd fds[2] = {{wl_display_get_fd(display), POLLIN, 0}, {STDIN_FILENO, POLLIN, 0}};
        if (poll(fds, 2, -1) < 0)
        {
            wl_display_cancel_read(display);
            if (errno == EINTR)
                continue;
            break;
        }
        if (fds[0].revents & POLLIN)
            wl_display_read_events(display);
        else
            wl_display_cancel_read(display);
        if (wl_display_dispatch_pending(display) < 0 || (fds[0].revents & (POLLERR | POLLHUP)))
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
            pending -= (size_t)(start - input);
            memmove(input, start, pending);
            if (pending == sizeof input - 1)
                break;
        }
    }
done:
    for (int i = 0; i < maximumSources; ++i)
        if (sources[i].fd >= 0)
            close(sources[i].fd);
    wl_display_disconnect(display);
    return 0;
}
