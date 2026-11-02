#pragma once
#include <QQuickPaintedItem>

// The picture of a Mac OS 9.2.2 scroll bar for the shell's Qt Quick panels, drawn by the same code
// as the Qt style (paintPlatinumScrollBar). PlatinumScrollBar.qml supplies the behaviour.
// Makes ScrollBarArt available to QML as Frankenstein 1.0 (once; the desktop and panel tests call it).
void registerScrollBarArt();

class ScrollBarArt : public QQuickPaintedItem
{
    Q_OBJECT
    Q_PROPERTY(Qt::Orientation orientation MEMBER m_orientation NOTIFY changed)
    Q_PROPERTY(qreal position MEMBER m_position NOTIFY changed) // as ScrollBar.position
    Q_PROPERTY(qreal size MEMBER m_size NOTIFY changed)         // as ScrollBar.size
    Q_PROPERTY(bool active MEMBER m_active NOTIFY changed)      // the window is active
    Q_PROPERTY(QString pressedPart MEMBER m_pressed NOTIFY changed) // "", "sub", "add" or "box"
  public:
    explicit ScrollBarArt(QQuickItem *parent = nullptr);
    void paint(QPainter *painter) override;

  signals:
    void changed();

  private:
    Qt::Orientation m_orientation = Qt::Vertical;
    qreal m_position = 0, m_size = 1;
    bool m_active = true;
    QString m_pressed;
};
