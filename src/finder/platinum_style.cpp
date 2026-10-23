#include "platinum_style.hpp"
#include "appearance/interface_font.hpp"
#include <QAction>
#include <QApplication>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileIconProvider>
#include <QFileSystemModel>
#include <QPainter>
#include <QSet>
#include <QMimeDatabase>
#include <QImageReader>
#include <QStyleFactory>
#include <QStyleOption>
namespace
{
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
                        icon->addPixmap(pixmap);
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
    case PM_DefaultFrameWidth:
        return 1;
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
        classicBevel(p, o->rect, o->palette.color(QPalette::Base), true);
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
    if (element == PE_PanelButtonCommand)
    {
        p->save();
        QRect r = o->rect.adjusted(0, 0, -1, -1);
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

void PlatinumStyle::drawComplexControl(ComplexControl control, const QStyleOptionComplex *option,
                                       QPainter *painter, const QWidget *widget) const
{
    const auto *scroll = qstyleoption_cast<const QStyleOptionSlider *>(option);
    if (control != CC_ScrollBar || !scroll)
    {
        QProxyStyle::drawComplexControl(control, option, painter, widget);
        return;
    }
    painter->save();
    painter->fillRect(option->rect, QColor("#cccccc"));
    const bool horizontal = scroll->orientation == Qt::Horizontal;
    for (auto part : {SC_ScrollBarSubLine, SC_ScrollBarAddLine, SC_ScrollBarSlider})
    {
        if (!(option->subControls & part))
            continue;
        const auto rect = subControlRect(control, option, part, widget);
        if (rect.isEmpty())
            continue;
        const bool pressed = (option->activeSubControls & part) && (option->state & State_Sunken);
        classicBevel(painter, rect, QColor(pressed ? "#bbbbbb" : "#dddddd"), pressed);
        painter->setPen((option->state & State_Enabled) ? QColor("#222222") : QColor("#888888"));
        if (part == SC_ScrollBarSlider)
        {
            if ((horizontal ? rect.width() : rect.height()) > 18)
                for (int offset : {-3, 0, 3})
                    if (horizontal)
                        painter->drawLine(rect.center() + QPoint(offset, -3),
                                          rect.center() + QPoint(offset, 3));
                    else
                        painter->drawLine(rect.center() + QPoint(-3, offset),
                                          rect.center() + QPoint(3, offset));
        }
        else
        {
            const bool reverse = part == SC_ScrollBarSubLine;
            const auto center = rect.center();
            const int direction = reverse ? -1 : 1;
            QPolygon arrow;
            if (horizontal)
                arrow << center + QPoint(direction * 3, 0) << center + QPoint(-direction * 2, -3)
                      << center + QPoint(-direction * 2, 3);
            else
                arrow << center + QPoint(0, direction * 3) << center + QPoint(-3, -direction * 2)
                      << center + QPoint(3, -direction * 2);
            painter->setBrush(painter->pen().color());
            painter->drawPolygon(arrow);
        }
    }
    painter->restore();
}
