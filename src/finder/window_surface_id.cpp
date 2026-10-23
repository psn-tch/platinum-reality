#include "window_surface_id.hpp"
#include <QGuiApplication>
#include <QLibraryInfo>
#include <QWindow>
#include <wayland-client-core.h>

quint32 finderSurfaceId(QWindow *window)
{
    const auto platform = QGuiApplication::platformName();
    if (!window || !window->isVisible() || !window->handle() ||
        (platform != "wayland" && platform != "wayland-egl") ||
        QLibraryInfo::version() != QVersionNumber(6, 12, 0))
        return 0;
    // Qt 6.12.0's Wayland winId() returns its wl_surface proxy. This is a
    // version-specific representation, verified in the installed library and
    // upstream source, not an XID or a cross-platform interpretation of WId.
    const auto id = window->winId();
    return id ? wl_proxy_get_id(reinterpret_cast<wl_proxy *>(id)) : 0;
}
