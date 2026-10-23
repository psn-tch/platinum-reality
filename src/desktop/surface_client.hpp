#pragma once
#include <QWaylandClient>
#include <QWaylandSurface>

// A surface can survive its client during connection teardown. A QPointer to
// the surface alone therefore does not make its client safe to dereference.
inline qint64 liveSurfaceClientPid(const QWaylandSurface *surface)
{
    const auto *client = surface ? surface->client() : nullptr;
    return client ? client->processId() : 0;
}
