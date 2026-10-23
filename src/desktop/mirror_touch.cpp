#include "mirror_touch.hpp"
#include <QCoreApplication>
#include <QHash>
#include <QLibraryInfo>
#include <QMap>
#include <QPointer>
#include <QQuickWindow>
#include <QScopedValueRollback>
#include <QTouchEvent>
#include <QtGui/private/qeventpoint_p.h>
#include <algorithm>
#include <cmath>
#include <utility>

// Event-point mutation is private Qt API. Re-audit before changing this boundary.
static_assert(QT_VERSION == QT_VERSION_CHECK(6, 12, 0));

namespace {
bool contains(QRectF rect, QPointF point)
{
    return point.x() >= rect.x() && point.y() >= rect.y() &&
           point.x() < rect.x() + rect.width() && point.y() < rect.y() + rect.height();
}
}

struct MirrorTouch::State
{
    struct Contact {
        std::optional<QRectF> picture; // Empty is a lifetime rejection, not a pending press.
        QEventPoint last;
    };
    struct Device {
        QMap<int, Contact> contacts;
        QMetaObject::Connection destroyed;
    };
    QPointer<QQuickWindow> window;
    QRectF source;
    QVector<QRectF> pictures;
    QHash<const QPointingDevice *, Device> devices;
    bool enabled = false;
    bool forwarding = false;
    QEvent *forwardedEvent = nullptr;

    int active(const Device &device) const
    {
        return std::count_if(device.contacts.cbegin(), device.contacts.cend(),
                             [](const Contact &c) { return c.picture.has_value(); });
    }
    QPointF mapped(QRectF picture, QPointF point) const
    {
        const auto p = source.topLeft() + (point - picture.topLeft()) *
                        (source.width() / picture.width());
        // An established contact keeps its original picture's transform as it
        // leaves that picture. Clamp at the canonical edge, never jump to a copy.
        return {std::clamp(p.x(), source.left(), std::nextafter(source.right(), source.left())),
                std::clamp(p.y(), source.top(), std::nextafter(source.bottom(), source.top()))};
    }
    void deliver(QTouchEvent &event)
    {
        if (!window) return;
        QScopedValueRollback guard(forwarding, true);
        QScopedValueRollback eventGuard(forwardedEvent, static_cast<QEvent *>(&event));
        QCoreApplication::sendEvent(window, &event);
    }
};

MirrorTouch::MirrorTouch(QQuickWindow *window) : m_state(std::make_unique<State>())
{
    m_state->window = window;
}

MirrorTouch::~MirrorTouch()
{
    cancel();
    if (m_state->window) m_state->window->removeEventFilter(this);
    for (const auto &device : std::as_const(m_state->devices)) disconnect(device.destroyed);
}

bool MirrorTouch::setLayout(const DisplayLayout &layout)
{
    auto &s = *m_state;
    if (!s.window || s.forwarding || hasActiveContacts() || layout.mode() != DisplayLayout::Mode::Mirror ||
        QLibraryInfo::version() != QVersionNumber(6, 12, 0)) return false;
    QRectF source;
    QVector<QRectF> pictures;
    for (const auto &output : layout.outputs()) {
        if (output.name == layout.primary()) source = output.logical;
        pictures.append(layout.mirrorPicture(output.name));
    }
    if (source.isEmpty()) return false;
    s.source = source;
    s.pictures = pictures;
    if (!s.enabled) s.window->installEventFilter(this);
    s.enabled = true;
    return true;
}

bool MirrorTouch::hasActiveContacts() const
{
    for (const auto &device : std::as_const(m_state->devices))
        if (m_state->active(device)) return true;
    return false;
}

bool MirrorTouch::cancel()
{
    auto &s = *m_state;
    if (s.forwarding) return false;
    // Delivery may enter a nested event loop. Don't retain a hash iterator over it.
    const auto devices = s.devices.keys();
    for (auto *device : devices) {
        auto it = s.devices.find(device);
        if (it == s.devices.end() || !s.active(it.value())) continue;
        for (auto &contact : it->contacts) contact.picture.reset();
        QTouchEvent event(QEvent::TouchCancel, device);
        s.deliver(event);
    }
    return true;
}

bool MirrorTouch::eventFilter(QObject *object, QEvent *event)
{
    auto &s = *m_state;
    if (object != s.window || !s.enabled || event == s.forwardedEvent ||
        (event->type() != QEvent::TouchBegin && event->type() != QEvent::TouchUpdate &&
         event->type() != QEvent::TouchEnd && event->type() != QEvent::TouchCancel)) return false;
    auto *touch = static_cast<QTouchEvent *>(event);
    const auto *device = touch->pointingDevice();
    if (!device) { touch->accept(); return true; }
    if (!s.devices.contains(device)) {
        State::Device state;
        state.destroyed = connect(device, &QObject::destroyed, this, [this, device] {
            // The QObject is already being destroyed: never dereference it or create
            // a new event with this dead device. QPA must send cancellation before loss.
            m_state->devices.remove(device);
        });
        s.devices.insert(device, state);
    }
    auto &state = s.devices[device];
    const int before = s.active(state);
    if (touch->type() == QEvent::TouchCancel) {
        state.contacts.clear();
        if (before) {
            QTouchEvent cancelled(QEvent::TouchCancel, device, touch->modifiers());
            cancelled.setTimestamp(touch->timestamp());
            s.deliver(cancelled);
        }
        touch->accept();
        return true;
    }
    QList<QEventPoint> mapped;
    QList<int> released;
    for (const auto &raw : touch->points()) {
        auto it = state.contacts.find(raw.id());
        if (it == state.contacts.end()) {
            State::Contact contact;
            if (raw.state() == QEventPoint::Pressed)
                for (const auto &picture : std::as_const(s.pictures))
                    if (contains(picture, raw.scenePosition())) { contact.picture = picture; break; }
            it = state.contacts.insert(raw.id(), contact);
        }
        if (raw.state() == QEventPoint::Released) released.append(raw.id());
        if (!it->picture) continue;
        QEventPoint point(raw);
        QMutableEventPoint::detach(point); // Never mutate the original/shared host point.
        const auto at = raw.state() == QEventPoint::Stationary ? it->last.scenePosition() :
                            s.mapped(*it->picture, raw.scenePosition());
        const auto global = s.window->mapToGlobal(at);
        const bool pressed = raw.state() == QEventPoint::Pressed;
        QMutableEventPoint::setPosition(point, at);
        QMutableEventPoint::setScenePosition(point, at);
        QMutableEventPoint::setGlobalPosition(point, global);
        QMutableEventPoint::setGlobalPressPosition(point, pressed ? global : it->last.globalPressPosition());
        QMutableEventPoint::setGlobalLastPosition(point, pressed ? global : it->last.globalPosition());
        QMutableEventPoint::setGlobalGrabPosition(point, pressed ? global : it->last.globalGrabPosition());
        const qreal scale = s.source.width() / it->picture->width();
        QMutableEventPoint::setEllipseDiameters(point, raw.ellipseDiameters() * scale);
        QMutableEventPoint::setVelocity(point, raw.velocity() * scale);
        QMutableEventPoint::setWindow(point, s.window);
        QMutableEventPoint::setTarget(point, nullptr);
        point.setAccepted(false);
        it->last = point;
        mapped.append(point);
    }
    for (int id : released) state.contacts.remove(id);
    const int after = s.active(state);
    if (!mapped.isEmpty()) {
        const auto type = before == 0 ? QEvent::TouchBegin : after == 0 ? QEvent::TouchEnd : QEvent::TouchUpdate;
        QTouchEvent translated(type, device, touch->modifiers(), mapped);
        translated.setTimestamp(touch->timestamp());
        s.deliver(translated);
    }
    // Reject the original as a whole, including letterboxes. Letting it fall
    // through can synthesize a phantom mouse press at the unmapped host position.
    touch->accept();
    return true;
}
