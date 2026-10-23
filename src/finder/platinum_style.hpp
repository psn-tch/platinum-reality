#pragma once
#include <QKeySequence>
class QAction;
class QApplication;
void setClassicShortcuts(QAction *, QKeySequence);
#include <QProxyStyle>
class QFileSystemModel;
void installPlatinumIcons(QFileSystemModel *model);
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
    void drawPrimitive(PrimitiveElement element, const QStyleOption *option, QPainter *painter,
                       const QWidget *widget = nullptr) const override;
    void drawComplexControl(ComplexControl control, const QStyleOptionComplex *option,
                            QPainter *painter, const QWidget *widget = nullptr) const override;
};
