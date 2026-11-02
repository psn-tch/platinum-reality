#pragma once
#include <QProcessEnvironment>
#include <QString>
#include <functional>

void advertiseOnlyDrawableDmabufFormats(); // dmabuf_formats.cpp

struct CompositorDmabufSupport
{
    bool available = false;
    QString reason;
};

struct CompositorGraphicsChoice
{
    QString integration;
    bool automatic = false;
    QString reason;
};

// Query the active Qt platform EGL display, not an unrelated /dev/dri node.
CompositorDmabufSupport compositorDmabufSupport();
// Explicit requests and non-OpenGL paths never invoke the EGL probe.
CompositorGraphicsChoice chooseCompositorGraphics(
    const QString &platform, const QProcessEnvironment &environment, bool dmabufPlugin,
    const std::function<CompositorDmabufSupport()> &probe);
QString configureCompositorGraphics();
