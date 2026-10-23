#pragma once
#include <QtGlobal>
class QWindow;

// Platform adapter for the qualified Qt Wayland version. Zero means unavailable.
quint32 finderSurfaceId(QWindow *window);
