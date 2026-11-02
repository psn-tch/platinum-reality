#include "scroll_bar_art.hpp"
#include "finder/platinum_style.hpp"
#include <QPainter>
#include <QQmlEngine>
#include <QStyleOptionSlider>
#include <cmath>

void registerScrollBarArt()
{
    static const int type = qmlRegisterType<ScrollBarArt>("Frankenstein", 1, 0, "ScrollBarArt");
    Q_UNUSED(type);
}

ScrollBarArt::ScrollBarArt(QQuickItem *parent) : QQuickPaintedItem(parent)
{
    setAntialiasing(false);
    connect(this, &ScrollBarArt::changed, this, [this] { update(); });
}

void ScrollBarArt::paint(QPainter *painter)
{
    // ScrollBar's size is the visible fraction and position its start: as a page over a range.
    constexpr int scale = 10000;
    const qreal size = std::clamp<qreal>(m_size, 0, 1);
    QStyleOptionSlider option;
    option.rect = QRect(0, 0, int(width()), int(height()));
    option.orientation = m_orientation;
    option.minimum = 0;
    option.maximum = size >= 1 ? 0 : int(std::lround((1 - size) * scale));
    option.pageStep = int(std::lround(size * scale));
    option.sliderPosition = option.sliderValue = int(std::lround(std::clamp<qreal>(m_position, 0, 1 - size) * scale));
    option.subControls = QStyle::SC_All;
    option.state = QStyle::State_Enabled;
    if (m_active)
        option.state |= QStyle::State_Active;
    if (isEnabled() == false)
        option.state &= ~QStyle::State_Enabled;
    const QStyle::SubControl pressed = m_pressed == "sub" ? QStyle::SC_ScrollBarSubLine
                                       : m_pressed == "add" ? QStyle::SC_ScrollBarAddLine
                                       : m_pressed == "box" ? QStyle::SC_ScrollBarSlider : QStyle::SC_None;
    if (pressed != QStyle::SC_None)
    {
        option.activeSubControls = pressed;
        option.state |= QStyle::State_Sunken;
    }
    paintPlatinumScrollBar(painter, option);
}
