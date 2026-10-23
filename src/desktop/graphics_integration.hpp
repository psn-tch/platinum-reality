#pragma once
#include <QFileInfo>
#include <QGuiApplication>
#include <QLibraryInfo>

// The legacy default requires EGL_WL_bind_wayland_display, which is absent on
// this nested Wayland/EGL display. Qt's maintained DMA-BUF v4 integration uses
// EGL image import/device feedback instead. Keep preview/software rendering and
// explicit backend choices independent of this native Wayland preference.
void advertiseOnlyDrawableDmabufFormats(); // dmabuf_formats.cpp

inline QString configureCompositorGraphics()
{
    advertiseOnlyDrawableDmabufFormats();
    const auto key=QByteArrayLiteral("QT_WAYLAND_HARDWARE_INTEGRATION");
    if (qEnvironmentVariableIsSet(key.constData())) return qEnvironmentVariable(key.constData());
    const auto rhi=qEnvironmentVariable("QSG_RHI_BACKEND");
    if (QGuiApplication::platformName()!="wayland" ||
        qEnvironmentVariable("QT_QUICK_BACKEND")=="software" ||
        (!rhi.isEmpty() && rhi!="opengl")) return {};
    const auto plugin=QLibraryInfo::path(QLibraryInfo::PluginsPath)+
        "/wayland-graphics-integration-server/libqt-wayland-compositor-linux-dmabuf-v1.so";
    if (QFileInfo(plugin).isFile()) {
        qputenv(key.constData(),"linux-dmabuf-v1");
        qInfo("Native compositor integration: linux-dmabuf-v1");
    }
    return qEnvironmentVariable(key.constData());
}
