#pragma once
#include <QPointF>
#include <QRect>
#include <QString>
#include <QVector>
#include <optional>

struct DisplayOutput
{
    QString name;
    QRect logical;
    double scale = 1;
    QString transform = "normal";
};

// Coordinates supplied by the host are already logical and rotated. Normalize
// their origin once; never multiply them by physical scale a second time.
// This value object performs no IPC, hardware changes or persistence.
class DisplayLayout
{
  public:
    enum class Mode { Extend, Mirror };
    // canvas: a host window larger than the displays (Mirror shows the main display inside the
    // bigger mirrored screens); outputs are then given in canvas coordinates, within it.
    static std::optional<DisplayLayout> create(QVector<DisplayOutput> outputs,
                                               QString primary, Mode mode, QString *error = nullptr,
                                               QSize canvas = {});
    static DisplayLayout single(QSize size);
    const QVector<DisplayOutput> &outputs() const { return m_outputs; }
    QString primary() const { return m_primary; }
    Mode mode() const { return m_mode; }
    QRect hostBounds() const { return m_hostBounds; }
    QRect desktopBounds() const;
    QRect primaryRect() const;
    QRect screenForWindow(QRect window) const;
    QPoint reachablePosition(QPoint requested, QSize contentSize, bool fitWhole = false) const;
    QRectF mirrorPicture(const QString &output) const;
    std::optional<QPointF> hostToDesktop(QPointF point) const;

  private:
    DisplayLayout() = default;
    QVector<DisplayOutput> m_outputs;
    QString m_primary;
    Mode m_mode = Mode::Extend;
    QRect m_hostBounds;
    QRect primaryHostRect() const;
    QVector<QRect> desktopScreens() const;
};
