#pragma once
#include "command_keys.hpp"
#include <QHash>
#include <QWaylandQuickShellSurfaceItem>

class QWaylandSeat;
class QWaylandXdgPopup;

class ClientSurfaceItem : public QWaylandQuickShellSurfaceItem
{
    Q_OBJECT
    Q_PROPERTY(bool deferDragRelease MEMBER m_deferDragRelease)
    // The monitors (manager.displayOutputs, scene coordinates) that menus are kept on.
    Q_PROPERTY(QVariantList screens MEMBER m_screens)
  public:
    explicit ClientSurfaceItem(QQuickItem *parent = nullptr);
    // A menu's Edit command, delivered as Command and this key (Linux evdev
    // code, e.g. 46 for C) would be: the application's own shortcut.
    Q_INVOKABLE bool sendCommand(uint evdevKey);
    // A menu command that is a plain key (Clear is Delete), without modifiers.
    Q_INVOKABLE bool sendKey(uint evdevKey);

  protected:
    bool eventFilter(QObject *, QEvent *) override;
    void hoverEnterEvent(QHoverEvent *) override;
    void keyPressEvent(QKeyEvent *) override;
    void keyReleaseEvent(QKeyEvent *) override;
    void focusOutEvent(QFocusEvent *) override;
    void mouseReleaseEvent(QMouseEvent *) override;
    void touchEvent(QTouchEvent *) override;
    void touchUngrabEvent() override;
    void wheelEvent(QWheelEvent *) override;

  private:
    bool sendCommandChord(QKeyEvent *);
    bool deliverChord(QWaylandSeat *, uint evdevKey, Qt::KeyboardModifiers, bool release);
    bool deliver(QWaylandSeat *, uint physicalKey, const CommandChord &, bool release);
    bool finishCommandChord(QKeyEvent *);
    void scheduleChildReconciliation();
    void reconcileChildren();
    void retainWindowFocus(QQuickItem *parent);
    void updateSubsurfaceGeometry();
    void constrainPopups(QQuickItem *parent);
    void constrainPopup(QWaylandQuickShellSurfaceItem *item, QWaylandXdgPopup *popup);
    QVariantList m_screens;
    bool m_deferDragRelease = false;
    bool m_childReconciliationPending = false;
    bool m_touching = false;
    // Physical native scan code -> the translated one the client received.
    QHash<quint32, quint32> m_commandChords;
};
