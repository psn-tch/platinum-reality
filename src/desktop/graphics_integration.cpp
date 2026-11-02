#include "graphics_integration.hpp"
#include <QDebug>
#include <QFileInfo>
#include <QGuiApplication>
#include <QLibraryInfo>
#include <QtGui/qpa/qplatformnativeinterface.h>
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <sys/stat.h>

namespace
{
bool extension(const char *list, const char *name)
{
    return list && QByteArray(list).split(' ').contains(name);
}
}

CompositorDmabufSupport compositorDmabufSupport()
{
    auto *native = QGuiApplication::platformNativeInterface();
    const auto display = native ? native->nativeResourceForIntegration("EglDisplay") : nullptr;
    if (!display)
        return {false, "no platform EGL display"};
    if (!extension(eglQueryString(display, EGL_EXTENSIONS), "EGL_EXT_image_dma_buf_import"))
        return {false, "platform EGL display cannot import DMA-BUF"};
    const auto queryDisplay = reinterpret_cast<PFNEGLQUERYDISPLAYATTRIBEXTPROC>(eglGetProcAddress("eglQueryDisplayAttribEXT"));
    const auto queryDevice = reinterpret_cast<PFNEGLQUERYDEVICESTRINGEXTPROC>(eglGetProcAddress("eglQueryDeviceStringEXT"));
    const auto queryFormats = reinterpret_cast<PFNEGLQUERYDMABUFFORMATSEXTPROC>(eglGetProcAddress("eglQueryDmaBufFormatsEXT"));
    if (!queryDisplay || !queryDevice || !queryFormats)
        return {false, "EGL device or DMA-BUF query unavailable"};
    EGLAttrib attribute = 0;
    if (!queryDisplay(display, EGL_DEVICE_EXT, &attribute) || !attribute)
        return {false, "platform EGL display has no queryable device"};
    const auto device = reinterpret_cast<EGLDeviceEXT>(attribute);
    const auto extensions = queryDevice(device, EGL_EXTENSIONS);
    const char *path = nullptr;
    if (extension(extensions, "EGL_EXT_device_drm_render_node"))
        path = queryDevice(device, EGL_DRM_RENDER_NODE_FILE_EXT);
    if ((!path || !*path) && extension(extensions, "EGL_EXT_device_drm"))
        path = queryDevice(device, EGL_DRM_DEVICE_FILE_EXT);
    struct stat node {};
    if (!path || !*path || stat(path, &node) != 0 || !S_ISCHR(node.st_mode))
        return {false, "platform EGL device has no usable DRM node"};
    EGLint formats = 0;
    // The existing Qt6.12 interposer filters this to formats our scene graph draws.
    if (!queryFormats(display, 0, nullptr, &formats) || formats <= 0)
        return {false, "platform EGL display has no drawable DMA-BUF formats"};
    return {true, {}};
}

CompositorGraphicsChoice chooseCompositorGraphics(
    const QString &platform, const QProcessEnvironment &environment, bool dmabufPlugin,
    const std::function<CompositorDmabufSupport()> &probe)
{
    for (const auto *key : {"QT_WAYLAND_HARDWARE_INTEGRATION", "QT_WAYLAND_CLIENT_BUFFER_INTEGRATION"})
        if (environment.contains(key))
            return {environment.value(key), false, {}};
    const auto rhi = environment.value("QSG_RHI_BACKEND");
    if (platform != "wayland" || environment.value("QT_QUICK_BACKEND") == "software" ||
        (!rhi.isEmpty() && rhi != "opengl") || !dmabufPlugin)
        return {};
    const auto support = probe();
    // Qt6.12 advertises DMA-BUF before checking its EGL device. On a software
    // display that incomplete global stalls Xwayland startup. Its known legacy
    // plugin keeps wl_shm available without advertising DMA-BUF. An unknown name
    // such as "none" does not disable plugins: Qt falls back to an available one.
    return {support.available ? "linux-dmabuf-v1" : "wayland-egl", true, support.reason};
}

QString configureCompositorGraphics()
{
    advertiseOnlyDrawableDmabufFormats();
    const auto plugin = QLibraryInfo::path(QLibraryInfo::PluginsPath) +
        "/wayland-graphics-integration-server/libqt-wayland-compositor-linux-dmabuf-v1.so";
    const auto choice = chooseCompositorGraphics(QGuiApplication::platformName(),
        QProcessEnvironment::systemEnvironment(), QFileInfo(plugin).isFile(), compositorDmabufSupport);
    if (choice.automatic)
    {
        qputenv("QT_WAYLAND_HARDWARE_INTEGRATION", choice.integration.toUtf8());
        qInfo().noquote() << "Native compositor integration:" << choice.integration
                         << (choice.reason.isEmpty() ? QString() : "(" + choice.reason + ")");
    }
    return choice.integration;
}
