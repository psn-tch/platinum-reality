#include "mirror_cursor.hpp"
#include <QImage>
#include <QMouseEvent>
#include <QPainter>
#include <QPointer>
#include <QPixmap>
#include <QQuickPaintedItem>
#include <QQuickWindow>
#include <QWaylandCompositor>
#include <QWaylandSeat>
#include <QWaylandSurface>
#include <QWaylandView>
#include <cmath>

namespace {
// Only this small texture changes when the cursor image changes. Motion updates item geometry;
// it must not repaint/upload an entire monitor-sized QQuickPaintedItem every frame.
class CursorImage final : public QQuickPaintedItem
{
  public:
    using QQuickPaintedItem::QQuickPaintedItem;
    QImage image;
    void setImage(const QImage &value)
    {
        if (image.cacheKey() == value.cacheKey()) return;
        image = value;
        setSize(image.deviceIndependentSize());
        update();
    }
    void paint(QPainter *painter) override { painter->drawImage(QPointF(), image); }
};

QImage arrow()
{
    // Original monochrome fallback, not a claim of pixel-authentic Apple cursor artwork.
    static const QImage image = [] {
        QImage result(13, 20, QImage::Format_ARGB32_Premultiplied);
        result.fill(Qt::transparent);
        QPainter painter(&result);
        painter.setPen(QPen(Qt::black, 1));
        painter.setBrush(Qt::white);
        painter.drawPolygon(QPolygon({{1,1},{1,15},{5,11},{8,18},{11,17},{8,10},{12,10}}));
        return result;
    }();
    return image;
}

bool valid(QRectF r)
{
    return std::isfinite(r.x()) && std::isfinite(r.y()) && std::isfinite(r.width()) &&
           std::isfinite(r.height()) && r.width() > 0 && r.height() > 0;
}
}

struct MirrorCursor::State
{
    MirrorCursor *owner = nullptr;
    QPointer<QWaylandCompositor> compositor;
    QPointer<QQuickWindow> window;
    CursorImage *image = nullptr;
    QRectF source;
    QPointF position;
    QCursor cursor;
    bool seen = false;
    bool queued = false;
    bool mappingValid = false;
    QMetaObject::Connection redraw;
    QMetaObject::Connection surfaceGone;

    void schedule()
    {
        if (queued) return;
        queued = true;
        QMetaObject::invokeMethod(owner, [this] { queued = false; refresh(); }, Qt::QueuedConnection);
    }

    void refresh()
    {
        QCursor selected(Qt::ArrowCursor);
        if (auto *seat = compositor ? compositor->defaultSeat() : nullptr)
            if (auto *focus = seat->mouseFocus())
                if (auto *item = qobject_cast<QQuickItem *>(focus->renderObject()))
                    selected = item->cursor();
        cursor = selected;
        const bool inside = mappingValid && seen && position.x() >= source.left() &&
            position.y() >= source.top() && position.x() < source.x() + source.width() &&
            position.y() < source.y() + source.height();
        image->setVisible(inside && selected.shape() != Qt::BlankCursor);
        if (!inside || selected.shape() == Qt::BlankCursor) return;
        auto pixels = selected.shape() == Qt::BitmapCursor ? selected.pixmap().toImage() : QImage();
        const bool fallback = pixels.isNull();
        if (fallback) pixels = arrow();
        const QPointF hotspot = fallback ? QPointF(1,1) : selected.hotSpot();
        const qreal scale = owner->width() / source.width();
        image->setImage(pixels);
        image->setScale(scale);
        image->setPosition((position - source.topLeft() - hotspot) * scale);
    }
};

MirrorCursor::MirrorCursor(QWaylandCompositor *compositor, QQuickWindow *window, QQuickItem *parent)
    : QQuickItem(parent), m_state(std::make_unique<State>())
{
    m_state->owner = this;
    m_state->compositor = compositor;
    m_state->window = window;
    m_state->image = new CursorImage(this);
    m_state->image->setTransformOrigin(QQuickItem::TopLeft);
    m_state->image->setVisible(false);
    setClip(true);
    setAcceptedMouseButtons(Qt::NoButton);
    m_state->image->setAcceptedMouseButtons(Qt::NoButton);
    if (window) window->installEventFilter(this);
    if (auto *seat = compositor ? compositor->defaultSeat() : nullptr)
        connect(seat, &QWaylandSeat::cursorSurfaceRequested, this,
            [this, seat](QWaylandSurface *surface, int, int, QWaylandClient *client) {
                auto *focus = seat->mouseFocus();
                if (!focus || !focus->surface() || focus->surface()->client() != client) return;
                disconnect(m_state->redraw);
                disconnect(m_state->surfaceGone);
                if (surface) {
                    m_state->redraw = connect(surface, &QWaylandSurface::redraw, this, [this] { m_state->schedule(); });
                    m_state->surfaceGone = connect(surface, &QObject::destroyed, this, [this] { m_state->schedule(); });
                }
                // ClientCursors owns the real cursor. Read its cached QCursor after its request
                // or redraw handler, rather than consuming the cursor surface's buffer twice.
                m_state->schedule();
            });
}

MirrorCursor::~MirrorCursor() = default;

void MirrorCursor::setMapping(QRectF source, QRectF picture)
{
    m_state->source = source;
    m_state->mappingValid = valid(source) && valid(picture) &&
        qAbs(picture.width() / source.width() - picture.height() / source.height()) < 0.0001;
    if (m_state->mappingValid) {
        setPosition(picture.topLeft());
        setSize(picture.size());
    }
    m_state->refresh();
}

QCursor MirrorCursor::presentedCursor() const { return m_state->cursor; }
bool MirrorCursor::cursorPresented() const { return m_state->image->isVisible(); }
QRectF MirrorCursor::cursorSceneRect() const
{
    return m_state->image->mapRectToScene(m_state->image->boundingRect());
}

bool MirrorCursor::eventFilter(QObject *object, QEvent *event)
{
    if (object == m_state->window) {
        if (event->type() == QEvent::MouseMove || event->type() == QEvent::MouseButtonPress ||
            event->type() == QEvent::MouseButtonRelease) {
            m_state->position = static_cast<QMouseEvent *>(event)->position();
            m_state->seen = true;
            m_state->schedule();
        } else if (event->type() == QEvent::Leave) {
            m_state->seen = false;
            m_state->schedule();
        }
    }
    return QQuickItem::eventFilter(object, event);
}
