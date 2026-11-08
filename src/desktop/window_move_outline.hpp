#pragma once
#include <QImage>
#include <QQuickImageProvider>
#include <memory>

// A private, in-memory snapshot of this compositor's own view. No disk capture.
// Only narrow perimeter strips are uploaded while the client stays in place.
struct WindowMoveSnapshot
{
    QImage image;
    qreal scale = 1;
    quint64 generation = 0;
};
QImage windowMoveStrip(const WindowMoveSnapshot &snapshot, QRect logical);
class WindowMoveOutlineProvider final : public QQuickImageProvider
{
  public:
    explicit WindowMoveOutlineProvider(std::shared_ptr<WindowMoveSnapshot> snapshot)
        : QQuickImageProvider(Image), m_snapshot(std::move(snapshot)) {}
    QImage requestImage(const QString &id, QSize *size, const QSize &requestedSize) override;
  private:
    std::shared_ptr<WindowMoveSnapshot> m_snapshot;
};
