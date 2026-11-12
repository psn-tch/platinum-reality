// Captures one output of the HOST compositor (an owned headless Sway) as a binary PPM on stdout,
// in logical coordinates (rotation and scale undone), like `grim -o NAME -s 1 -t ppm -`. Test
// tool only, so the lock tests need nothing outside this source tree. Never run against a
// user's live session.
//   output-capture NAME
// Uses ext-image-copy-capture-v1 with an output capture source; exit 1 on any failure.
#define _GNU_SOURCE
#include "ext-image-capture-source-client.h"
#include "ext-image-copy-capture-client.h"
#include "xdg-output-client.h"
#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>
#include <wayland-client.h>

struct output
{
    struct wl_output *output;
    struct zxdg_output_v1 *xdg;
    char *name;
    int32_t logicalWidth, logicalHeight;
};

static struct output outputs[16];
static int outputCount;
static struct wl_shm *shm;
static struct zxdg_output_manager_v1 *xdgManager;
static struct ext_output_image_capture_source_manager_v1 *sourceManager;
static struct ext_image_copy_capture_manager_v1 *captureManager;

static void outputName(void *data, struct wl_output *output, const char *name)
{
    (void)output;
    struct output *self = data;
    free(self->name);
    self->name = strdup(name);
}
static void outputGeometry(void *data, struct wl_output *output, int32_t x, int32_t y, int32_t width,
                           int32_t height, int32_t subpixel, const char *make, const char *model, int32_t transform)
{
    (void)data, (void)output, (void)x, (void)y, (void)width, (void)height, (void)subpixel, (void)make, (void)model,
        (void)transform;
}
static void outputMode(void *data, struct wl_output *output, uint32_t flags, int32_t width, int32_t height,
                       int32_t refresh)
{
    (void)data, (void)output, (void)flags, (void)width, (void)height, (void)refresh;
}
static void outputDone(void *data, struct wl_output *output) { (void)data, (void)output; }
static void outputScale(void *data, struct wl_output *output, int32_t factor) { (void)data, (void)output, (void)factor; }
static void outputDescription(void *data, struct wl_output *output, const char *description)
{
    (void)data, (void)output, (void)description;
}
static const struct wl_output_listener outputListener = {outputGeometry, outputMode,        outputDone,
                                                         outputScale,    outputName,        outputDescription};

static void xdgPosition(void *data, struct zxdg_output_v1 *xdg, int32_t x, int32_t y)
{
    (void)data, (void)xdg, (void)x, (void)y;
}
static void xdgSize(void *data, struct zxdg_output_v1 *xdg, int32_t width, int32_t height)
{
    (void)xdg;
    struct output *self = data;
    self->logicalWidth = width;
    self->logicalHeight = height;
}
static void xdgDone(void *data, struct zxdg_output_v1 *xdg) { (void)data, (void)xdg; }
static void xdgName(void *data, struct zxdg_output_v1 *xdg, const char *name) { (void)data, (void)xdg, (void)name; }
static void xdgDescription(void *data, struct zxdg_output_v1 *xdg, const char *description)
{
    (void)data, (void)xdg, (void)description;
}
static const struct zxdg_output_v1_listener xdgListener = {xdgPosition, xdgSize, xdgDone, xdgName, xdgDescription};

static void global(void *data, struct wl_registry *registry, uint32_t name, const char *interface,
                   uint32_t version)
{
    (void)data;
    if (!strcmp(interface, wl_output_interface.name) && version >= 4 &&
        outputCount < (int)(sizeof outputs / sizeof *outputs))
    {
        struct output *output = &outputs[outputCount++];
        output->output = wl_registry_bind(registry, name, &wl_output_interface, 4);
        wl_output_add_listener(output->output, &outputListener, output);
    }
    else if (!strcmp(interface, wl_shm_interface.name))
        shm = wl_registry_bind(registry, name, &wl_shm_interface, 1);
    else if (!strcmp(interface, zxdg_output_manager_v1_interface.name) && version >= 2)
        xdgManager = wl_registry_bind(registry, name, &zxdg_output_manager_v1_interface, 2);
    else if (!strcmp(interface, ext_output_image_capture_source_manager_v1_interface.name))
        sourceManager = wl_registry_bind(registry, name, &ext_output_image_capture_source_manager_v1_interface, 1);
    else if (!strcmp(interface, ext_image_copy_capture_manager_v1_interface.name))
        captureManager = wl_registry_bind(registry, name, &ext_image_copy_capture_manager_v1_interface, 1);
}
static void globalRemove(void *data, struct wl_registry *registry, uint32_t name)
{
    (void)data, (void)registry, (void)name;
}
static const struct wl_registry_listener registryListener = {global, globalRemove};

// The session's buffer constraints, then the frame's outcome.
static uint32_t bufferWidth, bufferHeight, shmFormat = UINT32_MAX;
static bool constraintsDone, stopped, ready, failed;
static uint32_t frameTransform = WL_OUTPUT_TRANSFORM_NORMAL;

static void bufferSize(void *data, struct ext_image_copy_capture_session_v1 *session, uint32_t width, uint32_t height)
{
    (void)data, (void)session;
    bufferWidth = width;
    bufferHeight = height;
}
static void sessionShmFormat(void *data, struct ext_image_copy_capture_session_v1 *session, uint32_t format)
{
    (void)data, (void)session;
    // Little-endian 32-bit RGB, the only layouts read below (Sway's pixman renderer offers
    // XRGB8888, its GLES2 renderer XBGR8888).
    if ((format == WL_SHM_FORMAT_XRGB8888 || format == WL_SHM_FORMAT_ARGB8888 ||
         format == WL_SHM_FORMAT_XBGR8888 || format == WL_SHM_FORMAT_ABGR8888) &&
        shmFormat == UINT32_MAX)
        shmFormat = format;
}
static void dmabufDevice(void *data, struct ext_image_copy_capture_session_v1 *session, struct wl_array *device)
{
    (void)data, (void)session, (void)device;
}
static void dmabufFormat(void *data, struct ext_image_copy_capture_session_v1 *session, uint32_t format,
                         struct wl_array *modifiers)
{
    (void)data, (void)session, (void)format, (void)modifiers;
}
static void sessionDone(void *data, struct ext_image_copy_capture_session_v1 *session)
{
    (void)data, (void)session;
    constraintsDone = true;
}
static void sessionStopped(void *data, struct ext_image_copy_capture_session_v1 *session)
{
    (void)data, (void)session;
    stopped = true;
}
static const struct ext_image_copy_capture_session_v1_listener sessionListener = {
    bufferSize, sessionShmFormat, dmabufDevice, dmabufFormat, sessionDone, sessionStopped};

static void frameTransformEvent(void *data, struct ext_image_copy_capture_frame_v1 *frame, uint32_t transform)
{
    (void)data, (void)frame;
    frameTransform = transform;
}
static void frameDamage(void *data, struct ext_image_copy_capture_frame_v1 *frame, int32_t x, int32_t y,
                        int32_t width, int32_t height)
{
    (void)data, (void)frame, (void)x, (void)y, (void)width, (void)height;
}
static void framePresentation(void *data, struct ext_image_copy_capture_frame_v1 *frame, uint32_t hi, uint32_t lo,
                              uint32_t nsec)
{
    (void)data, (void)frame, (void)hi, (void)lo, (void)nsec;
}
static void frameReady(void *data, struct ext_image_copy_capture_frame_v1 *frame)
{
    (void)data, (void)frame;
    ready = true;
}
static void frameFailed(void *data, struct ext_image_copy_capture_frame_v1 *frame, uint32_t reason)
{
    (void)data, (void)frame, (void)reason;
    failed = true;
}
static const struct ext_image_copy_capture_frame_v1_listener frameListener = {
    frameTransformEvent, frameDamage, framePresentation, frameReady, frameFailed};

// A point of the upright (transformed) image, relative to its centre, back to the buffer's
// orientation: the inverse of the rotation (counter-clockwise) and flip the compositor applied.
static void untransform(uint32_t transform, double x, double y, double *bx, double *by)
{
    double rx = x, ry = y;
    switch (transform & 3)
    {
    case 1: rx = y, ry = -x; break; // 90
    case 2: rx = -x, ry = -y; break; // 180
    case 3: rx = -y, ry = x; break; // 270
    }
    *bx = transform & 4 ? -rx : rx;
    *by = ry;
}

int main(int argc, char **argv)
{
    if (argc != 2)
    {
        fprintf(stderr, "usage: output-capture NAME\n");
        return 2;
    }
    struct wl_display *display = wl_display_connect(NULL);
    if (!display)
        return 1;
    struct wl_registry *registry = wl_display_get_registry(display);
    wl_registry_add_listener(registry, &registryListener, NULL);
    wl_display_roundtrip(display);
    if (!shm || !xdgManager || !sourceManager || !captureManager)
    {
        fprintf(stderr, "output-capture: the compositor lacks a needed protocol\n");
        return 1;
    }
    for (int i = 0; i < outputCount; ++i)
    {
        outputs[i].xdg = zxdg_output_manager_v1_get_xdg_output(xdgManager, outputs[i].output);
        zxdg_output_v1_add_listener(outputs[i].xdg, &xdgListener, &outputs[i]);
    }
    wl_display_roundtrip(display);
    struct output *target = NULL;
    for (int i = 0; i < outputCount; ++i)
        if (outputs[i].name && !strcmp(outputs[i].name, argv[1]))
            target = &outputs[i];
    if (!target || target->logicalWidth <= 0 || target->logicalHeight <= 0)
    {
        fprintf(stderr, "output-capture: no output %s\n", argv[1]);
        return 1;
    }

    struct ext_image_capture_source_v1 *source =
        ext_output_image_capture_source_manager_v1_create_source(sourceManager, target->output);
    struct ext_image_copy_capture_session_v1 *session =
        ext_image_copy_capture_manager_v1_create_session(captureManager, source, 0);
    ext_image_copy_capture_session_v1_add_listener(session, &sessionListener, NULL);
    while (!constraintsDone && !stopped)
        if (wl_display_dispatch(display) < 0)
            return 1;
    if (stopped || shmFormat == UINT32_MAX || !bufferWidth || !bufferHeight)
    {
        fprintf(stderr, "output-capture: no usable shm buffer for %s\n", argv[1]);
        return 1;
    }

    const size_t stride = (size_t)bufferWidth * 4, size = stride * bufferHeight;
    const int fd = memfd_create("output-capture", MFD_CLOEXEC);
    if (fd < 0 || ftruncate(fd, (off_t)size) < 0)
        return 1;
    uint8_t *pixels = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (pixels == MAP_FAILED)
        return 1;
    struct wl_shm_pool *pool = wl_shm_create_pool(shm, fd, (int32_t)size);
    struct wl_buffer *buffer = wl_shm_pool_create_buffer(pool, 0, (int32_t)bufferWidth, (int32_t)bufferHeight,
                                                         (int32_t)stride, shmFormat);
    struct ext_image_copy_capture_frame_v1 *frame = ext_image_copy_capture_session_v1_create_frame(session);
    ext_image_copy_capture_frame_v1_add_listener(frame, &frameListener, NULL);
    ext_image_copy_capture_frame_v1_attach_buffer(frame, buffer);
    ext_image_copy_capture_frame_v1_damage_buffer(frame, 0, 0, (int32_t)bufferWidth, (int32_t)bufferHeight);
    ext_image_copy_capture_frame_v1_capture(frame);
    while (!ready && !failed)
        if (wl_display_dispatch(display) < 0)
            return 1;
    if (failed)
    {
        fprintf(stderr, "output-capture: capture of %s failed\n", argv[1]);
        return 1;
    }

    // Nearest-neighbour sampling at each logical pixel's centre.
    const bool rotated = frameTransform & 1;
    const double width = rotated ? bufferHeight : bufferWidth, height = rotated ? bufferWidth : bufferHeight;
    const int32_t outWidth = target->logicalWidth, outHeight = target->logicalHeight;
    // Byte order in memory: B, G, R, X for the RGB formats, R, G, B, X for the BGR ones.
    const bool bgr = shmFormat == WL_SHM_FORMAT_XBGR8888 || shmFormat == WL_SHM_FORMAT_ABGR8888;
    const int red = bgr ? 0 : 2, blue = bgr ? 2 : 0;
    printf("P6\n%d %d\n255\n", outWidth, outHeight);
    uint8_t *row = malloc((size_t)outWidth * 3);
    if (!row)
        return 1;
    for (int32_t y = 0; y < outHeight; ++y)
    {
        for (int32_t x = 0; x < outWidth; ++x)
        {
            double bx, by;
            untransform(frameTransform, (x + 0.5) * width / outWidth - width / 2,
                        (y + 0.5) * height / outHeight - height / 2, &bx, &by);
            long px = lround(floor(bx + bufferWidth / 2.0)), py = lround(floor(by + bufferHeight / 2.0));
            px = px < 0 ? 0 : px >= (long)bufferWidth ? (long)bufferWidth - 1 : px;
            py = py < 0 ? 0 : py >= (long)bufferHeight ? (long)bufferHeight - 1 : py;
            const uint8_t *pixel = pixels + (size_t)py * stride + (size_t)px * 4;
            row[x * 3] = pixel[red];
            row[x * 3 + 1] = pixel[1];
            row[x * 3 + 2] = pixel[blue];
        }
        fwrite(row, 3, (size_t)outWidth, stdout);
    }
    free(row);
    return fflush(stdout) == 0 ? 0 : 1;
}
