#pragma once
#include <QKeySequence>
class QAction;
class QApplication;
void setClassicShortcuts(QAction *, QKeySequence);
#include <QProxyStyle>
class QFileSystemModel;
class QPainter;
class QStyleOptionSlider;
// Mac OS 9.2.2's scroll bar, as PlatinumStyle draws it (the shell's Qt Quick bars use it too).
void paintPlatinumScrollBar(QPainter *painter, const QStyleOptionSlider &option);
void installPlatinumIcons(QFileSystemModel *model);
class QMessageBox;
// Return presses the box's default (its ring), whichever button has focus, as in Mac OS 9; Space and
// clicks still choose the focused one. Qt makes dialog buttons auto-default, so a focused destructive
// button could take Return from a Cancel default (the installer's erase question did: Rocky, 2026-10-31).
void keepReturnOnTheDefault(QMessageBox &box);
class PlatinumStyle final : public QProxyStyle
{
  public:
    PlatinumStyle();
    void polish(QApplication *application) override;
    void polish(QPalette &palette) override;
    int pixelMetric(PixelMetric metric, const QStyleOption *option = nullptr,
                    const QWidget *widget = nullptr) const override;
    int styleHint(StyleHint hint, const QStyleOption *option = nullptr,
                  const QWidget *widget = nullptr,
                  QStyleHintReturn *returnData = nullptr) const override;
    QIcon standardIcon(StandardPixmap icon, const QStyleOption *option = nullptr,
                       const QWidget *widget = nullptr) const override;
    QSize sizeFromContents(ContentsType type, const QStyleOption *option, const QSize &size,
                           const QWidget *widget = nullptr) const override;
    void drawPrimitive(PrimitiveElement element, const QStyleOption *option, QPainter *painter,
                       const QWidget *widget = nullptr) const override;
    void drawComplexControl(ComplexControl control, const QStyleOptionComplex *option,
                            QPainter *painter, const QWidget *widget = nullptr) const override;
    QRect subControlRect(ComplexControl control, const QStyleOptionComplex *option, SubControl part,
                         const QWidget *widget = nullptr) const override;
    void polish(QWidget *widget) override;
    void unpolish(QWidget *widget) override;
    bool eventFilter(QObject *watched, QEvent *event) override;
};
