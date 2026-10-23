#include "client_surface_item.hpp"
#include "command_keys.hpp"
#include "popup_placement.hpp"
#include <QHoverEvent>
#include <QPointer>
#include <QQuickWindow>
#include <QWaylandXdgSurface>
#include <QWaylandXdgShell>
#include <QtMath>
#include <QKeyEvent>
#include <QLibraryInfo>
#include <QMouseEvent>
#include <QTimer>
#include <QVariant>
#include <QWaylandCompositor>
#include <QWaylandDrag>
#include <QWaylandPointer>
#include <QWaylandSeat>
#include <QWaylandSurface>
#include <QWaylandTouch>
#include <QWheelEvent>
#include <QtWaylandCompositor/private/qwaylandkeyboard_p.h>
#include <QtWaylandCompositor/private/qwaylandpointer_p.h>
#include <QtWaylandCompositor/private/qwaylandtouch_p.h>

namespace
{
// Qt 6.12 forwards scrolling as whole axis steps (angle / 12, truncated) with no source, stop
// or frame, so a touchpad's small two-finger steps reached applications as nothing. Send what
// the host reported instead: finger distance with its source and stop, or wheel notches with
// their discrete value, each closed by a frame for clients that wait for one.
bool forwardScroll(QWaylandQuickItem *item, QWheelEvent *event)
{
    auto *compositor = item->compositor();
    auto *surface = item->surface();
    auto *seat = compositor ? compositor->seatFor(event) : nullptr;
    auto *pointer = seat ? seat->pointer() : nullptr;
    if (!surface || !surface->client() || !pointer || !item->inputEventsEnabled() ||
        !item->inputRegionContains(event->position()))
        return false;
    if (pointer->mouseFocus() != item->view())
        seat->sendMouseMoveEvent(item->view(), item->mapToSurface(event->position()),
                                 item->mapToScene(event->position()));
    // Touchpad scrolling has phases and exact host pixels (none while a fraction builds up);
    // a wheel may have only its angle.
    const bool finger = event->phase() != Qt::NoScrollPhase;
    QPointF distance = finger ? QPointF() : -QPointF(event->angleDelta()) / 12.0; // 120 a notch
    if (!event->pixelDelta().isNull())
    {
        // Host pixels to this surface's units. Qt's deltas point up; Wayland's point down.
        const auto at = event->scenePosition();
        distance = item->mapToSurface(item->mapFromScene(at)) -
                   item->mapToSurface(item->mapFromScene(at + QPointF(event->pixelDelta())));
    }
    const bool stop = finger && event->phase() == Qt::ScrollEnd;
    if (distance.isNull() && !stop)
        return true;
    auto *pointerPrivate = static_cast<QWaylandPointerPrivate *>(QObjectPrivate::get(pointer));
    const uint32_t time = compositor->currentTimeMsecs();
    const uint32_t source = finger ? WL_POINTER_AXIS_SOURCE_FINGER : WL_POINTER_AXIS_SOURCE_WHEEL;
    for (auto *resource : pointerPrivate->resourceMap().values(surface->client()->client()))
    {
        const auto handle = resource->handle;
        const int version = resource->version();
        if (!distance.isNull())
        {
            if (version >= WL_POINTER_AXIS_SOURCE_SINCE_VERSION)
                pointerPrivate->send_axis_source(handle, source);
            for (const auto axis : {WL_POINTER_AXIS_VERTICAL_SCROLL, WL_POINTER_AXIS_HORIZONTAL_SCROLL})
            {
                const bool vertical = axis == WL_POINTER_AXIS_VERTICAL_SCROLL;
                const double value = vertical ? distance.y() : distance.x();
                const int angle = vertical ? event->angleDelta().y() : event->angleDelta().x();
                if (value == 0)
                    continue;
                if (!finger && version >= WL_POINTER_AXIS_VALUE120_SINCE_VERSION)
                    pointerPrivate->send_axis_value120(handle, axis, -angle);
                else if (!finger && version >= WL_POINTER_AXIS_DISCRETE_SINCE_VERSION && angle % 120 == 0)
                    pointerPrivate->send_axis_discrete(handle, axis, -angle / 120);
                pointerPrivate->send_axis(handle, time, axis, wl_fixed_from_double(value));
            }
            if (version >= WL_POINTER_FRAME_SINCE_VERSION)
                pointerPrivate->send_frame(handle);
        }
        // The fingers lifted: the application may now start kinetic scrolling.
        if (stop && version >= WL_POINTER_AXIS_STOP_SINCE_VERSION)
        {
            pointerPrivate->send_axis_source(handle, source);
            pointerPrivate->send_axis_stop(handle, time, WL_POINTER_AXIS_VERTICAL_SCROLL);
            pointerPrivate->send_axis_stop(handle, time, WL_POINTER_AXIS_HORIZONTAL_SCROLL);
            pointerPrivate->send_frame(handle);
        }
    }
    return true;
}

void cancelTouch(QWaylandSeat *seat, QWaylandSurface *surface)
{
    if (!seat || !seat->touch() || !surface || !surface->client()) return;
    const auto version = QLibraryInfo::version();
    if (version.majorVersion() != 6 || version.minorVersion() != 12)
    {
        seat->touch()->sendCancelEvent(surface->client());
        return;
    }
    auto *data = static_cast<QWaylandTouchPrivate *>(QObjectPrivate::get(seat->touch()));
    for (auto *resource : data->resourceMap().values(surface->client()->client()))
        wl_touch_send_cancel(resource->handle);
}

// Qt 6.12 sends item-local touch positions without mapToSurface(), as with hover-enter (see
// below), so a touch landed at 1/scale of its place: up and left of the finger on a scaled
// screen. Send the mapped positions. Other Qt versions must be audited independently.
bool forwardTouch(QWaylandQuickItem *item, QTouchEvent *event)
{
    const auto version = QLibraryInfo::version();
    auto *compositor = item->compositor();
    auto *surface = item->surface();
    auto *seat = compositor ? compositor->seatFor(event) : nullptr;
    auto *touch = seat ? seat->touch() : nullptr;
    const auto &points = event->points();
    if (version.majorVersion() != 6 || version.minorVersion() != 12 || !surface || !surface->client() ||
        !touch || !item->inputEventsEnabled() ||
        (event->type() == QEvent::TouchBegin &&
         (points.isEmpty() || !item->inputRegionContains(points.first().position()))))
        return false;
    if (event->type() == QEvent::TouchCancel)
    {
        cancelTouch(seat, surface);
        return true;
    }
    // Firefox/GTK binds this seat more than once. Qt 6.12's public touch
    // senders use resourceMap().value(client), so only one binding receives
    // the gesture (in Firefox this can be the binding that ignores GTK input).
    // Send the same mapped stream to every binding for this client and seat,
    // just as the scrolling path above does for pointer resources.
    auto *data = static_cast<QWaylandTouchPrivate *>(QObjectPrivate::get(touch));
    const auto resources = data->resourceMap().values(surface->client()->client());
    const auto time = compositor->currentTimeMsecs();
    for (const auto &point : points)
    {
        const auto at = item->mapToSurface(point.position());
        const auto serial = point.state() == QEventPoint::Pressed || point.state() == QEventPoint::Released
                                ? compositor->nextSerial() : 0;
        for (auto *resource : resources)
        {
            if (point.state() == QEventPoint::Pressed)
                wl_touch_send_down(resource->handle, serial, time, surface->resource(), point.id(),
                                   wl_fixed_from_double(at.x()), wl_fixed_from_double(at.y()));
            else if (point.state() == QEventPoint::Updated)
                wl_touch_send_motion(resource->handle, time, point.id(),
                                     wl_fixed_from_double(at.x()), wl_fixed_from_double(at.y()));
            else if (point.state() == QEventPoint::Released)
                wl_touch_send_up(resource->handle, serial, time, point.id());
        }
    }
    for (auto *resource : resources)
        wl_touch_send_frame(resource->handle);
    if (event->type() == QEvent::TouchBegin && item->focusOnClick())
        item->takeFocus(seat);
    return true;
}
} // namespace

ClientSurfaceItem::ClientSurfaceItem(QQuickItem *parent) : QWaylandQuickShellSurfaceItem(parent)
{
    connect(this, &QQuickItem::childrenChanged, this,
            &ClientSurfaceItem::scheduleChildReconciliation);
    connect(this, &QQuickItem::scaleChanged, this,
            &ClientSurfaceItem::scheduleChildReconciliation);
}

void ClientSurfaceItem::scheduleChildReconciliation()
{
    if (m_childReconciliationPending)
        return;
    m_childReconciliationPending = true;
    // Qt creates its normal subsurface views and stacking connections first.
    // Reconcile after its integer-rounded position handler has also run. A child
    // announced from its base constructor is not yet a QWaylandQuickItem either.
    QTimer::singleShot(0, this, &ClientSurfaceItem::reconcileChildren);
}

void ClientSurfaceItem::reconcileChildren()
{
    m_childReconciliationPending = false;
    retainWindowFocus(this);
    updateSubsurfaceGeometry();
    constrainPopups(this);
}

void ClientSurfaceItem::constrainPopups(QQuickItem *parent)
{
    for (auto *child : parent->childItems())
    {
        auto *item = qobject_cast<QWaylandQuickShellSurfaceItem *>(child);
        auto *xdg = item ? qobject_cast<QWaylandXdgSurface *>(item->shellSurface()) : nullptr;
        if (xdg && xdg->popup() && !item->property("platinumPopupConstrained").toBool())
        {
            item->setProperty("platinumPopupConstrained", true);
            constrainPopup(item, xdg->popup());
        }
        constrainPopups(child); // Submenus are popups of popups.
    }
}

// Qt 6.12's compositor places every popup at its unconstrained position ("TODO: check
// positioner constraints"). Applications cannot see screen positions, so a menu opened near an
// edge was cut off. Keep it on the monitor under its anchor, then follow the configure.
void ClientSurfaceItem::constrainPopup(QWaylandQuickShellSurfaceItem *item, QWaylandXdgPopup *popup)
{
    auto *parent = qobject_cast<QWaylandQuickItem *>(item->parentItem());
    if (!parent || !popup->parentXdgSurface())
        return;
    const auto windowOffset = popup->parentXdgSurface()->windowGeometry().topLeft();
    const auto toScene = [&](QPointF window) { return parent->mapToScene(parent->mapFromSurface(window + windowOffset)); };
    const auto toWindow = [&](QPointF scene) { return parent->mapToSurface(parent->mapFromScene(scene)) - windowOffset; };
    const auto anchor = toScene(QRectF(popup->anchorRect()).center());
    QRectF screen;
    qreal overlap = -1;
    const auto window = parent->mapRectToScene(parent->boundingRect());
    for (const auto &value : m_screens)
    {
        const auto map = value.toMap();
        const QRectF rect(map.value("x").toReal(), map.value("y").toReal(), map.value("width").toReal(),
                          map.value("height").toReal());
        const auto common = rect.intersected(window);
        const qreal score = rect.contains(anchor) ? 1e12 : common.width() * common.height();
        if (rect.isValid() && score > overlap)
        {
            overlap = score;
            screen = rect;
        }
    }
    if (screen.isEmpty() && this->window())
        screen = QRectF(QPointF(), this->window()->size());
    if (screen.isEmpty())
        return;
    // The monitor in the parent's window coordinates, rounded inward: never a pixel off screen.
    const auto low = toWindow(screen.topLeft()), high = toWindow(screen.bottomRight());
    const QPoint start(qCeil(low.x()), qCeil(low.y()));
    const QRect bounds(start, QSize(qFloor(high.x()) - start.x(), qFloor(high.y()) - start.y()));
    const auto placed = placePopup({popup->anchorRect(), popup->anchorEdges(), popup->gravityEdges(), popup->offset(),
                                    popup->positionerSize(), popup->flipConstraints(), popup->slideConstraints(),
                                    popup->resizeConstraints()},
                                   bounds);
    // A closing menu's surface still signals while its popup and parent are being destroyed.
    const auto follow = [item, parent = QPointer<QWaylandQuickItem>(parent),
                         popup = QPointer<QWaylandXdgPopup>(popup), windowOffset]
    {
        if (!parent || !popup)
            return;
        // In the parent's units, as ClientSurface sizes windows: Qt's implicit popup size is
        // rounded, which at a fractional host scale drew a menu a pixel or two off its place.
        const auto source = parent->surface() ? parent->surface()->destinationSize() : QSize();
        const auto size = item->surface() ? item->surface()->destinationSize() : QSize();
        if (source.width() > 0 && !size.isEmpty())
            item->setSize(QSizeF(size) * (parent->width() / source.width()));
        // After Qt's integration, which resets the item to the unconstrained position.
        const auto position = parent->mapFromSurface(QPointF(popup->configuredGeometry().topLeft() + windowOffset));
        if (item->moveItem() && item->moveItem()->position() != position)
            item->moveItem()->setPosition(position);
    };
    connect(popup, &QWaylandXdgPopup::configuredGeometryChanged, item, follow);
    if (item->surface())
        connect(item->surface(), &QWaylandSurface::destinationSizeChanged, item, follow);
    if (placed != popup->configuredGeometry())
        popup->sendConfigure(placed);
    follow();
}

void ClientSurfaceItem::retainWindowFocus(QQuickItem *parent)
{
    // The manager gives keyboard focus to the window's own xdg surface. Qt
    // deactivates a toplevel whenever keyboard focus moves to any other surface,
    // and the popup and subsurface items it creates take focus on click by
    // default. A click on a browser context menu therefore deactivated the
    // window, and the browser dismissed the menu before the item could activate.
    for (auto *child : parent->childItems())
    {
        auto *item = qobject_cast<QWaylandQuickItem *>(child);
        if (!item)
            continue;
        item->setFocusOnClick(false);
        // Popup and subsurface items are Qt's own; scroll through them as through this one.
        if (!qobject_cast<ClientSurfaceItem *>(item))
            item->installEventFilter(this);
        connect(item, &QQuickItem::childrenChanged, this,
                &ClientSurfaceItem::scheduleChildReconciliation, Qt::UniqueConnection);
        retainWindowFocus(item);
    }
}

void ClientSurfaceItem::updateSubsurfaceGeometry()
{
    const auto visit = [&](auto &&self, QQuickItem *parent) -> void
    {
        for (auto *child : parent->childItems())
        {
            // Xdg popups have their own positioner and may extend beyond a frame.
            if (qobject_cast<QWaylandQuickShellSurfaceItem *>(child))
                continue;
            auto *item = qobject_cast<QWaylandQuickItem *>(child);
            if (!item || !item->surface())
                continue;
            auto *surface = item->surface();
            connect(surface, &QWaylandSurface::destinationSizeChanged, this,
                    &ClientSurfaceItem::scheduleChildReconciliation, Qt::UniqueConnection);
            connect(surface, &QWaylandSurface::subsurfacePositionChanged, this,
                    &ClientSurfaceItem::scheduleChildReconciliation, Qt::UniqueConnection);
            const auto size = surface->destinationSize();
            if (size.width() > 0 && size.height() > 0)
            {
                // Do not map through the child's old width while a resize is
                // pending: that would retain the previous buffer dimensions.
                const auto local = QPointF(size.width(), size.height()) / scale();
                item->setSize(QSizeF(local.x(), local.y()));
            }
            // Manager records the public protocol signal from surface creation,
            // including positions sent before the first buffer maps its view.
            if (auto *owner = qobject_cast<QWaylandQuickItem *>(parent))
                item->setPosition(owner->mapFromSurface(
                    surface->property("frankensteinSubsurfacePosition").toPointF()));
            self(self, item);
        }
    };
    visit(visit, this);
}

void ClientSurfaceItem::mouseReleaseEvent(QMouseEvent *event)
{
    auto *seat = compositor() ? compositor()->seatFor(event) : nullptr;
    const bool endingDrag = surface() && seat && seat->drag()->origin() == surface() &&
                            event->button() == Qt::LeftButton && seat->pointer()->isButtonPressed();
    const bool defer = m_deferDragRelease;
    const bool trace = endingDrag && qEnvironmentVariable("FRANKENSTEIN_NATIVE_TEST") == "1";
    if (trace)
        qInfo("Drop source release: defer=%d input=%d", defer, inputEventsEnabled());
    QWaylandQuickShellSurfaceItem::mouseReleaseEvent(event);
    if (trace)
        qInfo("Drop source released: origin=%d pointer=%d", bool(seat->drag()->origin()),
              seat->pointer()->isButtonPressed());
    // Quick's drag release can end the data-device grab without balancing the
    // pointer button. Reconcile immediately, only if the public state is still
    // pressed after the base handler. Do not decrement an already released seat.
    if (endingDrag && !defer && seat->pointer()->isButtonPressed())
        seat->sendMouseReleaseEvent(event->button());
}

void ClientSurfaceItem::wheelEvent(QWheelEvent *event)
{
    if (forwardScroll(this, event))
        event->accept();
    else
        QWaylandQuickShellSurfaceItem::wheelEvent(event);
}

void ClientSurfaceItem::touchEvent(QTouchEvent *event)
{
    if (!forwardTouch(this, event))
    {
        QWaylandQuickShellSurfaceItem::touchEvent(event);
        return;
    }
    event->accept();
    m_touching = event->type() == QEvent::TouchBegin || event->type() == QEvent::TouchUpdate;
}

void ClientSurfaceItem::touchUngrabEvent()
{
    // The shell took the touch (a menu opened, the window moved): the client must let it go.
    if (m_touching && surface() && surface()->client())
        cancelTouch(compositor()->defaultSeat(), surface());
    m_touching = false;
    QWaylandQuickShellSurfaceItem::touchUngrabEvent();
}

bool ClientSurfaceItem::eventFilter(QObject *watched, QEvent *event)
{
    auto *item = qobject_cast<QWaylandQuickItem *>(watched);
    const bool touch = event->type() == QEvent::TouchBegin || event->type() == QEvent::TouchUpdate ||
                       event->type() == QEvent::TouchEnd || event->type() == QEvent::TouchCancel;
    if (item && ((event->type() == QEvent::Wheel && forwardScroll(item, static_cast<QWheelEvent *>(event))) ||
                 (touch && forwardTouch(item, static_cast<QTouchEvent *>(event)))))
    {
        event->accept();
        return true;
    }
    return QWaylandQuickShellSurfaceItem::eventFilter(watched, event);
}

void ClientSurfaceItem::hoverEnterEvent(QHoverEvent *event)
{
    const auto version = QLibraryInfo::version();
    // Qt 6.12 sends the item-local hover-enter position without mapToSurface(),
    // unlike its move and button paths. This item's scale makes the units differ,
    // so a client saw the pointer enter at two thirds of its real position.
    // Send the same enter from the mapped position. Audit other Qt versions.
    if (version.majorVersion() != 6 || version.minorVersion() != 12)
    {
        QWaylandQuickShellSurfaceItem::hoverEnterEvent(event);
        return;
    }
    if (!surface() || !inputEventsEnabled() || !inputRegionContains(event->position()))
    {
        event->ignore();
        return;
    }
    compositor()->seatFor(event)->sendMouseMoveEvent(view(), mapToSurface(event->position()),
                                                     mapToScene(event->position()));
}

// Command chords (see command_keys.hpp). The client is told the modifiers it
// expects for this one key only; the real state, maintained by Qt from the
// physical keys, is restored when the key is released. Qt 6.12.0 audited.
bool ClientSurfaceItem::deliverChord(QWaylandSeat *seat, uint evdevKey,
                                     Qt::KeyboardModifiers modifiers, bool release)
{
    QString appId;
    if (auto *xdg = qobject_cast<QWaylandXdgSurface *>(shellSurface()); xdg && xdg->toplevel())
        appId = xdg->toplevel()->appId();
    const auto chord = translateCommandKey(evdevKey, modifiers, isTerminalApplication(appId));
    return chord && deliver(seat, evdevKey, *chord, release);
}

bool ClientSurfaceItem::deliver(QWaylandSeat *seat, uint physicalKey, const CommandChord &chord,
                                bool release)
{
    if (QLibraryInfo::version() != QVersionNumber(6, 12, 0) || !seat || !surface() ||
        !surface()->client() || !inputEventsEnabled())
        return false;
    auto *keyboard = seat->keyboard();
    if (!keyboard || !seat->setKeyboardFocus(surface()))
        return false;
    auto *state = QWaylandKeyboardPrivate::get(keyboard);
    state->maybeUpdateXkbScanCodeTable();
    auto *resource = state->resourceMap().value(surface()->client()->client());
    if (!state->xkbState() || !resource)
        return false;
    const uint32_t depressed = (chord.control ? 1u << state->controlIndex : 0) |
                               (chord.shift ? 1u << state->shiftIndex : 0);
    state->send_modifiers(resource->handle, compositor()->nextSerial(), depressed,
                          state->modsLatched, state->modsLocked, state->group);
    keyboard->sendKeyPressEvent(chord.key + 8);
    if (release)
    {
        keyboard->sendKeyReleaseEvent(chord.key + 8);
        state->send_modifiers(resource->handle, compositor()->nextSerial(), state->modsDepressed,
                              state->modsLatched, state->modsLocked, state->group);
    }
    else
        m_commandChords.insert(physicalKey + 8, chord.key + 8);
    return true;
}

bool ClientSurfaceItem::sendCommandChord(QKeyEvent *event)
{
    const auto native = event->nativeScanCode();
    if (m_commandChords.contains(native))
    {
        event->accept(); // Clients repeat keys themselves.
        return true;
    }
    if (native < 8 || event->isAutoRepeat() ||
        !deliverChord(compositor()->seatFor(event), native - 8, event->modifiers(), false))
        return false;
    event->accept();
    return true;
}

bool ClientSurfaceItem::sendCommand(uint evdevKey)
{
    return compositor() && deliverChord(compositor()->defaultSeat(), evdevKey, Qt::MetaModifier, true);
}

bool ClientSurfaceItem::sendKey(uint evdevKey)
{
    return compositor() && deliver(compositor()->defaultSeat(), evdevKey, CommandChord{evdevKey}, true);
}

bool ClientSurfaceItem::finishCommandChord(QKeyEvent *event)
{
    const auto found = m_commandChords.constFind(event->nativeScanCode());
    if (found == m_commandChords.cend())
        return false;
    event->accept();
    if (event->isAutoRepeat())
        return true;
    const auto sent = found.value();
    m_commandChords.erase(found);
    auto *seat = compositor()->seatFor(event);
    auto *keyboard = seat ? seat->keyboard() : nullptr;
    if (!keyboard)
        return true;
    keyboard->sendKeyReleaseEvent(sent);
    auto *focus = seat->keyboardFocus();
    auto *state = QWaylandKeyboardPrivate::get(keyboard);
    if (auto *resource = focus && focus->client()
                             ? state->resourceMap().value(focus->client()->client())
                             : nullptr)
        state->send_modifiers(resource->handle, compositor()->nextSerial(), state->modsDepressed,
                              state->modsLatched, state->modsLocked, state->group);
    return true;
}

void ClientSurfaceItem::keyReleaseEvent(QKeyEvent *event)
{
    if (!finishCommandChord(event))
        QWaylandQuickShellSurfaceItem::keyReleaseEvent(event);
}

void ClientSurfaceItem::focusOutEvent(QFocusEvent *event)
{
    // A chord can open a dialog or switch windows before its physical key-up.
    // The release then belongs to another item. Forget this item's translation
    // now, otherwise its next ordinary press of that key is swallowed.
    if (!m_commandChords.isEmpty()) {
        auto *seat = compositor() ? compositor()->defaultSeat() : nullptr;
        auto *keyboard = seat ? seat->keyboard() : nullptr;
        if (keyboard && seat->keyboardFocus() == surface()) {
            for (const auto sent : std::as_const(m_commandChords))
                keyboard->sendKeyReleaseEvent(sent);
            if (surface() && surface()->client())
                keyboard->sendKeyModifiers(surface()->client(), compositor()->nextSerial());
        }
        m_commandChords.clear();
    }
    QWaylandQuickShellSurfaceItem::focusOutEvent(event);
}

void ClientSurfaceItem::keyPressEvent(QKeyEvent *event)
{
    if (sendCommandChord(event))
        return;
    const auto version = QLibraryInfo::version();
    // Qt 6.12's modifier-repair path does not represent Meta, while its native
    // window-system handler already maintains the complete XKB modifier state.
    // Prevent that repair from clearing native Meta before a letter is sent.
    // Preserve the original event for Qt's subsequent native-state bookkeeping.
    // Other Qt versions must be audited independently; do not assume the defect.
    if (version.majorVersion() == 6 && version.minorVersion() == 12 &&
        event->nativeScanCode() != 0 && event->modifiers().testFlag(Qt::MetaModifier))
    {
        auto modifiers = event->modifiers();
        modifiers.setFlag(Qt::MetaModifier, false);
        QKeyEvent forwarded(event->type(), event->key(), modifiers, event->nativeScanCode(),
                            event->nativeVirtualKey(), event->nativeModifiers(), event->text(),
                            event->isAutoRepeat(), event->count(), event->device());
        forwarded.setTimestamp(event->timestamp());
        QWaylandQuickShellSurfaceItem::keyPressEvent(&forwarded);
        event->setAccepted(forwarded.isAccepted());
        return;
    }
    QWaylandQuickShellSurfaceItem::keyPressEvent(event);
}
