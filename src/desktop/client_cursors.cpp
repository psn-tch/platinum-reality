#include "client_cursors.hpp"
#include <QCursor>
#include <QPixmap>
#include <QPointer>
#include <QQuickItem>
#include <QWaylandBufferRef>
#include <QWaylandClient>
#include <QWaylandCompositor>
#include <QWaylandSeat>
#include <QWaylandSurface>
#include <QWaylandView>

struct ClientCursors::State
{
    ClientCursors *owner = nullptr;
    QWaylandCompositor *compositor = nullptr;
    QPointer<QWaylandSurface> surface;
    std::unique_ptr<QWaylandView> view;
    QPoint hotspot;
    bool hidden = false;
    QPointer<QQuickItem> target;
    QMetaObject::Connection redraw;

    QWaylandView *pointerFocus() const
    {
        auto *seat = compositor->defaultSeat();
        return seat ? seat->mouseFocus() : nullptr;
    }

    QCursor cursor()
    {
        if (hidden)
            return QCursor(Qt::BlankCursor);
        if (!surface || !view)
            return QCursor(Qt::ArrowCursor);
        view->advance();
        const auto buffer = view->currentBuffer();
        if (!buffer.hasBuffer() || !buffer.isSharedMemory())
            return QCursor(Qt::ArrowCursor);
        const auto image = buffer.image();
        if (image.isNull())
            return QCursor(Qt::ArrowCursor);
        auto pixmap = QPixmap::fromImage(image.copy());
        pixmap.setDevicePixelRatio(qMax(1, surface->bufferScale()));
        return QCursor(pixmap, hotspot.x(), hotspot.y());
    }

    void refresh()
    {
        auto *focus = pointerFocus();
        auto *item = focus ? qobject_cast<QQuickItem *>(focus->renderObject()) : nullptr;
        if (target && target != item)
            target->unsetCursor();
        target = item;
        if (item)
            item->setCursor(cursor());
        // Cursor surfaces are never drawn in the scene, so answer their frame requests here.
        if (surface)
        {
            surface->frameStarted();
            surface->sendFrameCallbacks();
        }
    }

    void request(QWaylandSurface *requested, int x, int y, QWaylandClient *client)
    {
        // Only the client that has the pointer may choose its cursor.
        auto *focus = pointerFocus();
        if (!focus || !focus->surface() || focus->surface()->client() != client)
            return;
        if (surface != requested || !requested)
        {
            QObject::disconnect(redraw);
            view.reset();
            surface = requested;
            if (requested)
            {
                view = std::make_unique<QWaylandView>();
                view->setSurface(requested);
                redraw = QObject::connect(requested, &QWaylandSurface::redraw, owner, [this] { refresh(); });
            }
        }
        hidden = !requested;
        hotspot = QPoint(x, y);
        refresh();
    }
};

ClientCursors::ClientCursors(QWaylandCompositor *compositor, QObject *parent)
    : QObject(parent), m_state(std::make_unique<State>())
{
    m_state->owner = this;
    m_state->compositor = compositor;
    if (auto *seat = compositor ? compositor->defaultSeat() : nullptr)
        connect(seat, &QWaylandSeat::cursorSurfaceRequested, this,
                [this](QWaylandSurface *surface, int x, int y, QWaylandClient *client)
                { m_state->request(surface, x, y, client); });
}

ClientCursors::~ClientCursors()
{
    if (m_state->target)
        m_state->target->unsetCursor();
}
