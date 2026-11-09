#include "platinum_style.hpp"
#include "appearance/interface_font.hpp"
#include <QAction>
#include <QAbstractScrollArea>
#include <QApplication>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileIconProvider>
#include <QFileSystemModel>
#include <QPainter>
#include <QScrollBar>
#include <QSet>
#include <QMessageBox>
#include <QMenu>
#include <QPushButton>
#include <QMimeDatabase>
#include <QImageReader>
#include <QLineEdit>
#include <QStyleFactory>
#include <QStyleOption>
namespace
{
constexpr int focusRing = 2;
bool finderContextMenu(const QWidget *widget)
{
    return widget && widget->property("platinumFinderContextMenu").toBool();
}
const QColor focusRingColor("#666699");
void classicBevel(QPainter *painter, QRect rect, QColor fill, bool sunken = false)
{
    painter->fillRect(rect, fill);
    painter->setBrush(Qt::NoBrush);
    painter->setPen(QColor("#222222"));
    painter->drawRect(rect.adjusted(0, 0, -1, -1));
    rect.adjust(1, 1, -2, -2);
    painter->setPen(sunken ? QColor("#777777") : QColor(Qt::white));
    painter->drawLine(rect.topLeft(), rect.topRight());
    painter->drawLine(rect.topLeft(), rect.bottomLeft());
    painter->setPen(sunken ? QColor(Qt::white) : QColor("#888888"));
    painter->drawLine(rect.bottomLeft(), rect.bottomRight());
    painter->drawLine(rect.topRight(), rect.bottomRight());
}
class PlatinumIcons final : public QObject, public QFileIconProvider
{
  public:
    explicit PlatinumIcons(QObject *parent) : QObject(parent)
    {
        // The user's own icons (for example imported from their Mac OS 9 installation by
        // scripts/import-macos9-assets.py); never bundled. Missing files keep the drawings below.
        const auto folder = qEnvironmentVariable("PLATINUM_PERSONAL_ASSETS");
        if (QDir::isAbsolutePath(folder))
            for (auto [name, icon] : {std::pair{"folder", &m_folder}, std::pair{"document", &m_document}})
                for (const auto *suffix : {"", "-16"})
                {
                    const QFileInfo file(folder + "/icons/" + name + suffix + ".png");
                    QPixmap pixmap;
                    if (file.isFile() && !file.isSymLink() && file.size() <= 256 * 1024 && pixmap.load(file.filePath()))
                    {
                        icon->addPixmap(pixmap);
                        // Optional user-supplied state artwork, paired with this
                        // normal image. Never infer its mapping from PNG colors.
                        const QFileInfo selected(folder + "/icons/" + name + suffix + "-selected.png");
                        const QSize expected = *suffix ? QSize(16,16) : QSize(32,32);
                        QImageReader reader(selected.filePath());
                        if (selected.isFile() && !selected.isSymLink() && selected.size() <= 256 * 1024 &&
                            reader.size()==expected)
                            if (const auto image=reader.read(); !image.isNull())
                                icon->addPixmap(QPixmap::fromImage(image),QIcon::Selected);
                    }
                }
    }
    QIcon icon(const QFileInfo &info) const override
    {
        // Files show what they are: a picture's own thumbnail, else the icon theme's icon for
        // the file's type. Folders, links and unknown types keep the Platinum (or the user's
        // own Mac OS 9) drawings.
        if (!info.isDir() && !info.isSymLink())
        {
            if (auto picture = thumbnail(info); !picture.isNull())
                return picture;
            if (auto type = typeIcon(info); !type.isNull())
                return type;
        }
        const auto &personal = info.isDir() ? m_folder : m_document;
        if (!personal.isNull() && !info.isSymLink())
            return personal;
        QPixmap image(32, 32);
        image.fill(Qt::transparent);
        QPainter p(&image);
        p.setPen(QColor("#222"));
        if (info.isDir())
        {
            p.setBrush(QColor("#93afd2"));
            p.drawRect(2, 8, 12, 7);
            p.drawRect(2, 12, 28, 17);
            p.setPen(QColor("#e4eeff"));
            p.drawLine(3, 13, 29, 13);
            p.drawLine(3, 13, 3, 28);
            p.setPen(QColor("#4f6d95"));
            p.drawLine(4, 27, 29, 27);
            p.drawLine(29, 14, 29, 28);
        }
        else
        {
            QPolygon paper;
            paper << QPoint(5, 2) << QPoint(21, 2) << QPoint(27, 8) << QPoint(27, 30)
                  << QPoint(5, 30);
            p.setBrush(Qt::white);
            p.drawPolygon(paper);
            p.drawLine(21, 2, 21, 8);
            p.drawLine(21, 8, 27, 8);
            p.setPen(QColor("#777"));
            for (int y = 13; y < 27; y += 4)
                p.drawLine(9, y, 23, y);
        }
        if (info.isSymLink())
        {
            p.setPen(Qt::black);
            p.drawLine(0, 27, 9, 19);
            p.drawLine(9, 19, 9, 25);
            p.drawLine(9, 19, 3, 19);
        }
        return QIcon(image);
    }

  private:
    // A small framed thumbnail, decoded at that size (JPEG scales while decoding). Bounded input.
    static QIcon thumbnail(const QFileInfo &info)
    {
        static const QSet<QString> pictures{"png", "jpg", "jpeg", "gif", "webp", "bmp"};
        if (!pictures.contains(info.suffix().toLower()) || info.size() > 30 * 1024 * 1024)
            return {};
        QImageReader reader(info.filePath());
        reader.setAutoTransform(true);
        const auto size = reader.size();
        if (!size.isValid() || size.width() > 20000 || size.height() > 20000)
            return {};
        // Fill the square (the middle of a wide screenshot, say): at icon size a letterboxed
        // thumbnail is a sliver.
        const auto cover = size.scaled(62, 62, Qt::KeepAspectRatioByExpanding).expandedTo(QSize(1, 1));
        reader.setScaledSize(cover);
        const auto image = reader.read();
        if (image.isNull())
            return {};
        QImage canvas(64, 64, QImage::Format_ARGB32_Premultiplied);
        canvas.fill(Qt::transparent);
        QPainter painter(&canvas);
        const QRect area(1, 1, 62, 62);
        painter.drawImage(area, image, QRect((image.width() - 62) / 2, (image.height() - 62) / 2, 62, 62));
        painter.setPen(QColor("#555"));
        painter.drawRect(area.adjusted(0, 0, -1, -1));
        painter.end();
        return QIcon(QPixmap::fromImage(canvas));
    }
    // The icon theme's icon for the file's type; nothing for an unknown type.
    static QIcon typeIcon(const QFileInfo &info)
    {
        static const QMimeDatabase types;
        const auto type = types.mimeTypeForFile(info, QMimeDatabase::MatchExtension);
        if (!type.isValid() || type.isDefault())
            return {};
        auto icon = QIcon::fromTheme(type.iconName());
        return icon.isNull() ? QIcon::fromTheme(type.genericIconName()) : icon;
    }
    QIcon m_folder, m_document;
};

// Mac OS 9.2.2 scroll bars with Smart Scrolling (both arrows at the end, a scroll box sized to the
// view), measured pixel by pixel in a 9.2.2 Finder window: 16 px across with black edges, 14 px
// arrow boxes, a recessed gray track and a lavender scroll box with four grip lines. Worked out
// for a vertical bar; a horizontal one is the same picture, transposed.
struct ScrollGeometry
{
    QRect groove, slider, subLine, addLine;
};
ScrollGeometry scrollGeometry(int length, int thickness, const QStyleOptionSlider *s)
{
    ScrollGeometry g;
    const int last = length - 1;
    g.addLine = QRect(0, last - 14, thickness, 14);
    g.subLine = QRect(0, last - 29, thickness, 14);
    // The scroll box's black ends lie on the track's black edges (the bar's end and the line above
    // the arrows), so the box travels from row 0 to the line above the arrows.
    const int grooveStart = 0, grooveEnd = last - 30;
    if (grooveEnd - grooveStart + 1 < 18)
        return g; // Too short for a track: arrows only, as on a Mac.
    g.groove = QRect(0, grooveStart, thickness, grooveEnd - grooveStart + 1);
    if (s->maximum > s->minimum)
    {
        const int grooveLength = g.groove.height();
        const qint64 range = qint64(s->maximum) - s->minimum;
        int size = int(qint64(grooveLength) * std::max(1, s->pageStep) / (range + std::max(1, s->pageStep)));
        size = std::clamp(size, std::min(16, grooveLength), grooveLength);
        const int position = QStyle::sliderPositionFromValue(s->minimum, s->maximum, s->sliderPosition,
                                                             grooveLength - size, s->upsideDown);
        g.slider = QRect(0, grooveStart + position, thickness, size);
    }
    return g;
}
QRect alongBar(QRect r, const QStyleOptionSlider *s)
{
    if (s->orientation == Qt::Horizontal)
        r = QRect(r.y(), r.x(), r.height(), r.width());
    return r.translated(s->rect.topLeft());
}
void span(QPainter *p, int x0, int x1, int y, const char *color)
{
    if (x1 >= x0)
        p->fillRect(QRect(x0, y, x1 - x0 + 1, 1), QColor(color));
}
void column(QPainter *p, int x, int y0, int y1, const char *color)
{
    if (y1 >= y0)
        p->fillRect(QRect(x, y0, 1, y1 - y0 + 1), QColor(color));
}
// A 14 x 14 arrow box (inner columns 1..t-2) whose top row is y0; up points to the bar's start.
void arrowBox(QPainter *p, int t, int y0, bool up, bool pressed, bool live)
{
    const int right = t - 2, bottom = y0 + 13;
    if (!live)
        p->fillRect(QRect(1, y0, t - 2, 14), QColor("#eeeeee"));
    else
    {
        p->fillRect(QRect(1, y0, t - 2, 14), QColor(pressed ? "#bbbbbb" : "#dddddd"));
        span(p, 1, right - 1, y0, pressed ? "#888888" : "#ffffff");
        column(p, 1, y0, bottom - 1, pressed ? "#888888" : "#ffffff");
        column(p, right, y0 + 1, bottom, pressed ? "#eeeeee" : "#bbbbbb");
        span(p, 2, right, bottom, pressed ? "#eeeeee" : "#bbbbbb");
    }
    for (int row = 0; row < 4; ++row)
    {
        const int width = 2 * (up ? row + 1 : 4 - row);
        const int x0 = 1 + (t - 2 - width) / 2;
        span(p, x0, x0 + width - 1, y0 + 5 + row, live ? "#000000" : "#888888");
    }
}
void scrollBar(QPainter *p, const QStyleOptionSlider *s)
{
    const bool horizontal = s->orientation == Qt::Horizontal;
    const int length = horizontal ? s->rect.width() : s->rect.height();
    const int t = horizontal ? s->rect.height() : s->rect.width();
    const auto g = scrollGeometry(length, t, s);
    p->save();
    p->translate(s->rect.topLeft());
    if (horizontal)
        p->setWorldTransform(QTransform(0, 1, 1, 0, 0, 0), true);
    const int last = length - 1;
    // An inactive window's scroll bar is an empty strip.
    if (!(s->state & QStyle::State_Active))
    {
        p->fillRect(QRect(0, 0, t, length), QColor("#eeeeee"));
        p->setPen(QColor("#555555"));
        p->drawRect(QRect(0, 0, t - 1, length - 1));
        p->restore();
        return;
    }
    const bool live = (s->state & QStyle::State_Enabled) && s->maximum > s->minimum;
    p->fillRect(QRect(0, 0, t, length), QColor("#eeeeee"));
    p->setPen(Qt::black);
    p->drawRect(QRect(0, 0, t - 1, length - 1));
    const auto pressed = [&](QStyle::SubControl part)
    { return (s->activeSubControls & part) && (s->state & QStyle::State_Sunken); };
    arrowBox(p, t, g.subLine.top(), true, pressed(QStyle::SC_ScrollBarSubLine), live);
    arrowBox(p, t, g.addLine.top(), false, pressed(QStyle::SC_ScrollBarAddLine), live);
    span(p, 1, t - 2, last - 15, live ? "#000000" : "#888888");
    span(p, 1, t - 2, last - 30, live ? "#000000" : "#555555");
    if (!g.groove.isNull() && live)
    {
        const int top = g.groove.top() + 1, bottom = g.groove.bottom() - 1; // inside the black edges
        p->fillRect(QRect(1, top, t - 2, bottom - top + 1), QColor("#aaaaaa"));
        column(p, 1, top, bottom, "#777777");
        column(p, 2, top, bottom, "#888888");
        column(p, t - 3, top, bottom, "#bbbbbb");
        column(p, t - 2, top, bottom, "#cccccc");
        // Shadow rows stop at the track's light right edge, which runs unbroken.
        span(p, 1, t - 3, top, "#777777");
        span(p, 2, t - 4, top + 1, "#888888");
        if (!g.slider.isNull())
        {
            const int a = g.slider.top(), b = g.slider.bottom();
            // The scroll box casts the track's shadow below it.
            if (b + 1 <= bottom)
                span(p, 1, t - 3, b + 1, "#777777");
            if (b + 2 <= bottom)
                span(p, 2, t - 4, b + 2, "#888888");
            const bool dragging = pressed(QStyle::SC_ScrollBarSlider);
            span(p, 1, t - 2, a, "#000000");
            span(p, 1, t - 2, b, "#000000");
            p->fillRect(QRect(1, a + 1, t - 2, std::max(0, b - a - 1)), QColor(dragging ? "#7777dd" : "#9999ff"));
            column(p, 1, a + 1, b - 2, "#ccccff"); // the light edge stops above the bottom row
            span(p, 1, 1, a + 1, "#eeeeee");
            span(p, 2, t - 3, a + 1, "#ccccff");
            column(p, t - 2, a + 2, b - 1, "#6666cc");
            span(p, 2, t - 2, b - 1, "#6666cc");
            if (b - a >= 20)
            {
                const int start = (a + b + 1) / 2 - 4;
                for (int i = 0; i < 4; ++i)
                {
                    span(p, 4, 4, start + 2 * i, "#eeeeee");
                    span(p, 5, 10, start + 2 * i, "#ccccff");
                    span(p, 5, 11, start + 2 * i + 1, "#333399");
                }
            }
        }
    }
    p->restore();
}
} // namespace
void setClassicShortcuts(QAction *action, QKeySequence sequence)
{
    QList<QKeySequence> keys;
    if (!sequence.isEmpty())
    {
        // Explicit native adaptation: Command is Meta in our applications;
        // retain Ctrl alternatives for Linux input and nested host shortcuts.
        if (sequence.count() == 1 && (sequence[0].keyboardModifiers() & Qt::ControlModifier))
        {
            auto modifiers = sequence[0].keyboardModifiers();
            modifiers.setFlag(Qt::ControlModifier, false);
            modifiers.setFlag(Qt::MetaModifier);
            keys.push_back(QKeySequence(QKeyCombination(modifiers, sequence[0].key())));
        }
        keys.push_back(sequence);
    }
    action->setShortcuts(keys);
}
void keepReturnOnTheDefault(QMessageBox &box)
{
    // Keep these button/default settings on the dialog that receives input.
    box.setOption(QMessageBox::Option::DontUseNativeDialog);
    for (auto *button : box.buttons())
        if (auto *push = qobject_cast<QPushButton *>(button))
            push->setAutoDefault(false);
    if (auto *preferred = box.defaultButton())
        preferred->setFocus(Qt::OtherFocusReason);
}
void installPlatinumIcons(QFileSystemModel *model)
{
    model->setIconProvider(new PlatinumIcons(model));
}
PlatinumStyle::PlatinumStyle() : QProxyStyle(QStyleFactory::create("Fusion")) {}
void PlatinumStyle::polish(QApplication *application)
{
    QProxyStyle::polish(application);
    application->setFont(classicInterfaceFont());
}
void PlatinumStyle::polish(QPalette &p)
{
    p.setColor(QPalette::Window, QColor("#dddddd"));
    p.setColor(QPalette::Button, QColor("#dddddd"));
    p.setColor(QPalette::Base, Qt::white);
    p.setColor(QPalette::Text, Qt::black);
    p.setColor(QPalette::WindowText, Qt::black);
    p.setColor(QPalette::ButtonText, Qt::black);
    p.setColor(QPalette::Highlight, QColor("#333366"));
    p.setColor(QPalette::HighlightedText, Qt::white);
    p.setColor(QPalette::Light, Qt::white);
    p.setColor(QPalette::Mid, QColor("#888888"));
    p.setColor(QPalette::Dark, QColor("#444444"));
    for (auto role : {QPalette::Text, QPalette::WindowText, QPalette::ButtonText})
        p.setColor(QPalette::Disabled, role, QColor("#777777"));
}
int PlatinumStyle::pixelMetric(PixelMetric metric, const QStyleOption *option,
                               const QWidget *widget) const
{
    switch (metric)
    {
    case PM_ScrollBarExtent:
        return 16;
    case PM_ButtonMargin:
        return 5;
    case PM_ButtonDefaultIndicator:
        return 4; // Room for Mac OS 9's ring: 3 dark pixels and a 1-pixel gap.
    case PM_DefaultFrameWidth:
        // A text field keeps room for its focus ring, so it doesn't shift when focus moves.
        return qobject_cast<const QLineEdit *>(widget) ? 1 + focusRing : 1;
    case PM_MenuPanelWidth:
        return 1;
    case PM_IndicatorWidth:
    case PM_IndicatorHeight:
    case PM_ExclusiveIndicatorWidth:
    case PM_ExclusiveIndicatorHeight:
        return 13;
    default:
        return QProxyStyle::pixelMetric(metric, option, widget);
    }
}
// Qt makes room for the default ring only on auto-default (dialog) buttons. A window's own
// default button (the installer's Continue) gets the same room here, around its bevel.
static bool ringedOutsideDialog(const QStyleOption *option)
{
    const auto *button = qstyleoption_cast<const QStyleOptionButton *>(option);
    return button && (button->features & QStyleOptionButton::DefaultButton) &&
           !(button->features & QStyleOptionButton::AutoDefaultButton);
}
QSize PlatinumStyle::sizeFromContents(ContentsType type, const QStyleOption *option, const QSize &size,
                                      const QWidget *widget) const
{
    auto result = QProxyStyle::sizeFromContents(type, option, size, widget);
    if (type == CT_MenuItem && finderContextMenu(widget))
        if (const auto *item=qstyleoption_cast<const QStyleOptionMenuItem *>(option))
            result.setHeight(item->menuItemType==QStyleOptionMenuItem::Separator ? 6 :
                             qMax(16,item->fontMetrics.tightBoundingRect(item->text).height()+3));
    if (type == CT_PushButton && ringedOutsideDialog(option))
    {
        const auto ring = 2 * pixelMetric(PM_ButtonDefaultIndicator, option, widget);
        result += QSize(ring, ring);
    }
    return result;
}
int PlatinumStyle::styleHint(StyleHint hint, const QStyleOption *option, const QWidget *widget,
                             QStyleHintReturn *returnData) const
{
    if (hint == SH_DialogButtonBox_ButtonsHaveIcons)
        return 0;
    if (hint == SH_DialogButtonLayout)
        return QDialogButtonBox::MacLayout;
    return QProxyStyle::styleHint(hint, option, widget, returnData);
}
QIcon PlatinumStyle::standardIcon(StandardPixmap icon, const QStyleOption *option,
                                  const QWidget *widget) const
{
    if (icon != SP_MessageBoxQuestion && icon != SP_MessageBoxWarning &&
        icon != SP_MessageBoxCritical && icon != SP_MessageBoxInformation)
        return QProxyStyle::standardIcon(icon, option, widget);
    // Original geometric alert artwork; no Apple or host icon-theme assets.
    QPixmap image(32, 32);
    image.fill(Qt::transparent);
    QPainter painter(&image);
    painter.setPen(QPen(QColor("#222"), 2));
    painter.setBrush(QColor("#eee"));
    if (icon == SP_MessageBoxWarning)
        painter.drawPolygon(QPolygon({QPoint(16, 2), QPoint(30, 29), QPoint(2, 29)}));
    else
        painter.drawEllipse(2, 2, 28, 28);
    QFont font("DejaVu Sans", 18, QFont::Bold);
    painter.setFont(font);
    painter.drawText(image.rect(), Qt::AlignCenter,
                     icon == SP_MessageBoxQuestion      ? "?"
                     : icon == SP_MessageBoxInformation ? "i"
                     : icon == SP_MessageBoxCritical    ? "×"
                                                        : "!");
    return QIcon(image);
}
void PlatinumStyle::drawPrimitive(PrimitiveElement element, const QStyleOption *o, QPainter *p,
                                  const QWidget *w) const
{
    if (element==PE_FrameMenu && finderContextMenu(w))
        return; // The measured panel owns its boundary; no Fusion frame overlay.
    if (element==PE_PanelMenu && finderContextMenu(w))
    {
        p->save();
        const auto r=o->rect;
        p->fillRect(r,Qt::black);
        p->fillRect(r.adjusted(1,1,-1,-1),QColor("#dddddd"));
        p->fillRect(r.left()+1,r.top()+1,r.width()-3,1,Qt::white);
        p->fillRect(r.left()+1,r.top()+2,1,r.height()-4,Qt::white);
        p->fillRect(r.right()-1,r.top()+2,1,r.height()-3,QColor("#999999"));
        p->fillRect(r.left()+2,r.bottom()-1,r.width()-3,1,QColor("#999999"));
        p->restore();return;
    }
    if (element == PE_IndicatorCheckBox || element == PE_IndicatorRadioButton)
    {
        p->save();
        const auto r = o->rect.adjusted(1, 1, -2, -2);
        const auto ink = (o->state & State_Enabled) ? QColor("#222222") : QColor("#888888");
        p->setPen(ink);
        p->setBrush(Qt::white);
        if (element == PE_IndicatorRadioButton)
        {
            p->drawEllipse(r);
            if (o->state & State_On)
            {
                p->setBrush(ink);
                p->drawEllipse(r.adjusted(3, 3, -3, -3));
            }
        }
        else
        {
            classicBevel(p, o->rect, Qt::white, true);
            p->setPen(QPen(ink, 2));
            if (o->state & State_NoChange)
                p->drawLine(r.left() + 2, r.center().y(), r.right() - 2, r.center().y());
            else if (o->state & State_On)
            {
                p->drawLine(r.left() + 2, r.center().y(), r.center().x(), r.bottom() - 2);
                p->drawLine(r.center().x(), r.bottom() - 2, r.right() - 1, r.top() + 2);
            }
        }
        p->restore();
        return;
    }
    if (element == PE_PanelLineEdit)
    {
        p->save();
        auto field = o->rect;
        if (qobject_cast<const QLineEdit *>(w))
        {
            // Mac OS 9's focus ring: two pixels around the text field that has the keyboard.
            const bool focused = (o->state & State_HasFocus) && (o->state & State_Enabled);
            p->fillRect(o->rect, focused ? focusRingColor : o->palette.color(QPalette::Window));
            field.adjust(focusRing, focusRing, -focusRing, -focusRing);
        }
        classicBevel(p, field, o->palette.color(QPalette::Base), true);
        p->restore();
        return;
    }
    if (element == PE_PanelMenu)
    {
        p->save();
        classicBevel(p, o->rect, QColor("#eeeeee"));
        p->restore();
        return;
    }
    if (element == PE_FrameDefaultButton)
    {
        // The default button's thick rounded ring (Return presses it). Qt asks only while it is enabled.
        p->save();
        p->setRenderHint(QPainter::Antialiasing);
        p->setPen(QPen(QColor("#222222"), 3));
        p->setBrush(Qt::NoBrush);
        p->drawRoundedRect(QRectF(o->rect).adjusted(1.5, 1.5, -1.5, -1.5), 4, 4);
        p->restore();
        return;
    }
    if (element == PE_PanelButtonCommand)
    {
        p->save();
        QRect r = o->rect.adjusted(0, 0, -1, -1);
        if (ringedOutsideDialog(o))
        {
            const auto ring = pixelMetric(PM_ButtonDefaultIndicator, o, w);
            r.adjust(ring, ring, -ring, -ring);
        }
        bool pressed = o->state & State_Sunken;
        p->fillRect(r, pressed ? QColor("#bbbbbb") : QColor("#dddddd"));
        p->setPen(QColor("#222222"));
        p->drawRect(r);
        p->setPen(pressed ? QColor("#777777") : Qt::white);
        p->drawLine(r.topLeft() + QPoint(1, 1), r.topRight() + QPoint(-1, 1));
        p->drawLine(r.topLeft() + QPoint(1, 1), r.bottomLeft() + QPoint(1, -1));
        p->setPen(pressed ? Qt::white : QColor("#777777"));
        p->drawLine(r.bottomLeft() + QPoint(1, -1), r.bottomRight() + QPoint(-1, -1));
        p->drawLine(r.topRight() + QPoint(-1, 1), r.bottomRight() + QPoint(-1, -1));
        p->restore();
        return;
    }
    QProxyStyle::drawPrimitive(element, o, p, w);
}

void paintPlatinumScrollBar(QPainter *painter, const QStyleOptionSlider &option)
{
    scrollBar(painter, &option);
}
void PlatinumStyle::drawControl(ControlElement element, const QStyleOption *option, QPainter *p,
                                const QWidget *widget) const
{
    const auto *item=qstyleoption_cast<const QStyleOptionMenuItem *>(option);
    if(element!=CE_MenuItem || !item || !finderContextMenu(widget) ||
       (item->menuItemType!=QStyleOptionMenuItem::Normal &&
        item->menuItemType!=QStyleOptionMenuItem::DefaultItem &&
        item->menuItemType!=QStyleOptionMenuItem::Separator &&
        item->menuItemType!=QStyleOptionMenuItem::SubMenu))
    {
        QProxyStyle::drawControl(element,option,p,widget);return;
    }
    p->save();
    const auto r=item->rect;
    p->setClipRect(r);
    if(item->menuItemType==QStyleOptionMenuItem::Separator)
    {
        p->fillRect(r.left(),r.top()+2,r.width(),1,QColor("#888888"));
        p->fillRect(r.left(),r.top()+3,r.width(),1,Qt::white);
        p->restore();return;
    }
    const bool enabled=item->state & State_Enabled;
    const bool selected=enabled && (item->state & State_Selected);
    if(selected)
    {
        p->fillRect(r,QColor("#333399"));
        p->fillRect(r.left(),r.top(),1,r.height(),QColor("#6666cc"));
        p->fillRect(r.right(),r.top(),1,r.height(),QColor("#000088"));
    }
    p->setPen(!enabled ? QColor("#777777") : selected ? QColor(Qt::white) : QColor(Qt::black));
    auto font=item->font;
    if(item->menuItemType==QStyleOptionMenuItem::DefaultItem)font.setBold(true);
    p->setFont(font);
    const auto parts=item->text.split('\t');
    const int iconSpace=item->maxIconWidth>0 ? qMax(16,item->maxIconWidth) : 0;
    const auto textRect=r.adjusted(20+iconSpace,0,-13,0);
    p->drawText(textRect,Qt::AlignVCenter|Qt::AlignLeft|Qt::TextShowMnemonic,parts.first());
    if(parts.size()>1)
    {
        auto shortcut=parts.last();shortcut.replace("Meta+",QString::fromUtf8("⌘"));
        p->drawText(textRect,Qt::AlignVCenter|Qt::AlignRight,shortcut);
    }
    if(!item->icon.isNull())
        item->icon.paint(p,QRect(r.left()+20,r.center().y()-7,16,16),Qt::AlignCenter,
                         enabled ? QIcon::Normal : QIcon::Disabled,item->checked ? QIcon::On : QIcon::Off);
    if(item->checked)
    {
        p->setPen(QPen(p->pen().color(),2));
        p->drawPolyline(QPolygon{QPoint(r.left()+5,r.center().y()),QPoint(r.left()+8,r.center().y()+3),
                                 QPoint(r.left()+13,r.center().y()-4)});
    }
    if(item->menuItemType==QStyleOptionMenuItem::SubMenu)
    {
        p->setBrush(p->pen().color());p->setPen(Qt::NoPen);
        p->drawPolygon(QPolygon{QPoint(r.right()-8,r.center().y()-3),QPoint(r.right()-5,r.center().y()),
                                QPoint(r.right()-8,r.center().y()+3)});
    }
    p->restore();
}
void PlatinumStyle::drawComplexControl(ComplexControl control, const QStyleOptionComplex *option,
                                       QPainter *painter, const QWidget *widget) const
{
    const auto *scroll = qstyleoption_cast<const QStyleOptionSlider *>(option);
    if (control != CC_ScrollBar || !scroll)
    {
        QProxyStyle::drawComplexControl(control, option, painter, widget);
        return;
    }
    scrollBar(painter, scroll);
}
QRect PlatinumStyle::subControlRect(ComplexControl control, const QStyleOptionComplex *option,
                                    SubControl part, const QWidget *widget) const
{
    const auto *s = qstyleoption_cast<const QStyleOptionSlider *>(option);
    if (control != CC_ScrollBar || !s)
        return QProxyStyle::subControlRect(control, option, part, widget);
    const bool horizontal = s->orientation == Qt::Horizontal;
    const int length = horizontal ? s->rect.width() : s->rect.height();
    const int thickness = horizontal ? s->rect.height() : s->rect.width();
    const auto g = scrollGeometry(length, thickness, s);
    QRect r;
    switch (part)
    {
    case SC_ScrollBarSubLine: r = g.subLine; break;
    case SC_ScrollBarAddLine: r = g.addLine; break;
    case SC_ScrollBarGroove: r = g.groove; break;
    case SC_ScrollBarSlider: r = g.slider; break;
    case SC_ScrollBarSubPage:
        if (!g.slider.isNull())
            r = QRect(0, g.groove.top(), thickness, g.slider.top() - g.groove.top());
        break;
    case SC_ScrollBarAddPage:
        if (!g.slider.isNull())
            r = QRect(0, g.slider.bottom() + 1, thickness, g.groove.bottom() - g.slider.bottom());
        break;
    default: break;
    }
    return r.isEmpty() ? QRect() : alongBar(r, s);
}
void PlatinumStyle::polish(QWidget *widget)
{
    QProxyStyle::polish(widget);
    // A scroll bar is drawn empty in an inactive window, so it repaints when its window changes.
    if (qobject_cast<QScrollBar *>(widget))
        widget->installEventFilter(this);
    // Mac OS 9 scroll bars never come and go: a scrolling view always shows its vertical one.
    // Pop-up lists, menus and tooltips keep theirs as needed.
    if (auto *area = qobject_cast<QAbstractScrollArea *>(widget);
        area && area->verticalScrollBarPolicy() == Qt::ScrollBarAsNeeded && !area->inherits("QComboBoxListView"))
    {
        const auto type = area->window()->windowType();
        if (type != Qt::Popup && type != Qt::ToolTip)
            area->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOn);
    }
}
void PlatinumStyle::unpolish(QWidget *widget)
{
    if (qobject_cast<QScrollBar *>(widget))
        widget->removeEventFilter(this);
    QProxyStyle::unpolish(widget);
}
bool PlatinumStyle::eventFilter(QObject *watched, QEvent *event)
{
    if (event->type() == QEvent::WindowActivate || event->type() == QEvent::WindowDeactivate)
        if (auto *bar = qobject_cast<QScrollBar *>(watched))
            bar->update();
    return QProxyStyle::eventFilter(watched, event);
}
