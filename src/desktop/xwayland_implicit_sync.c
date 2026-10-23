/* Preloaded only into Platinum Reality' private Xwayland, by the launcher in
 * xwayland-launcher.sh, which xwayland-satellite finds first on its PATH.
 *
 * Xwayland 24.1 offers DRI3 1.4 syncobjs to X11 clients whenever the kernel
 * supports them, even when its Wayland compositor has no linux-drm-syncobj-v1
 * (Qt 6.12's compositor has none). Xwayland then signals a client's release
 * point only when wl_buffer.release reaches it. If Xwayland exits first, the
 * point never materializes, and Mesa's X11 Vulkan WSI waits for it forever in
 * vkAcquireNextImageKHR (drmSyncobjTimelineWait, WAIT_AVAILABLE, no timeout):
 * a connected game survives the desktop as a hung process. Xwayland uses this
 * call only to probe for syncobj support and on the path that probe enables.
 * Refusing it keeps X11 clients on the implicit-sync Present path, whose
 * acquire returns VK_ERROR_SURFACE_LOST_KHR when the display goes away.
 * Synchronization with this compositor is implicit end to end either way.
 * Evidence and the version audit: docs/vulkan-qualification.md. */
#include <errno.h>
#include <stdint.h>
#include <stdlib.h>

__attribute__((constructor)) static void forgetPreload(void)
{
    /* Xwayland's own helpers, such as xkbcomp, need not load this library. */
    unsetenv("LD_PRELOAD");
}

__attribute__((visibility("default"))) int drmSyncobjEventfd(int fd, uint32_t handle,
                                                             uint64_t point, int eventFd,
                                                             uint32_t flags)
{
    (void)fd;
    (void)handle;
    (void)point;
    (void)eventFd;
    (void)flags;
    /* libdrm reports ioctl failures as -1 with errno, like a kernel without
     * DRM_IOCTL_SYNCOBJ_EVENTFD. Xwayland treats anything but ENOENT as absent. */
    errno = EOPNOTSUPP;
    return -1;
}
