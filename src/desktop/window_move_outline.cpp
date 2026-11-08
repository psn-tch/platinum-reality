#include "window_move_outline.hpp"
#include <QtMath>

QImage windowMoveStrip(const WindowMoveSnapshot &snapshot, QRect logical)
{
    // A move paints four thin strips, never allocates a window-sized intermediate.
    if (!logical.isValid() || snapshot.scale <= 0 ||
        snapshot.scale > 8 || logical.width() > 65536 || logical.height() > 65536 ||
        logical.x() < -65536 || logical.x() > 65536 || logical.y() < -65536 || logical.y() > 65536 ||
        (logical.width() > 2 && logical.height() > 2))
        return {};
    QImage result(QSize(qCeil(logical.width() * snapshot.scale),
                        qCeil(logical.height() * snapshot.scale)), QImage::Format_ARGB32_Premultiplied);
    result.fill(Qt::transparent);
    const int sourceX = qFloor(logical.x() * snapshot.scale);
    const int sourceY = qFloor(logical.y() * snapshot.scale);
    for (int y = 0; y < result.height(); ++y)
    {
        auto *row = reinterpret_cast<QRgb *>(result.scanLine(y));
        const int ly = logical.y() + qFloor(y / snapshot.scale);
        for (int x = 0; x < result.width(); ++x)
        {
            const int lx = logical.x() + qFloor(x / snapshot.scale);
            if (snapshot.image.isNull())
            {
                // If a render backend cannot grab the view, retain a high-contrast
                // outline and identical policy semantics instead of opaque motion.
                row[x] = ((lx + ly) & 1) ? qRgb(255,255,255) : qRgb(0,0,0);
                continue;
            }
            if (((lx + ly) & 1) && snapshot.image.rect().contains(sourceX + x, sourceY + y))
            {
                const auto pixel = snapshot.image.pixel(sourceX + x, sourceY + y);
                row[x] = qRgb(255 - qRed(pixel), 255 - qGreen(pixel), 255 - qBlue(pixel));
            }
        }
    }
    return result;
}
QImage WindowMoveOutlineProvider::requestImage(const QString &id, QSize *size, const QSize &)
{
    // epoch/x/y/width/height; stale epochs cannot expose a previous move's snapshot.
    const auto parts = id.split('/');
    if (parts.size() != 5)
        return {};
    bool ok = false;
    const auto generation = parts[0].toULongLong(&ok);
    if (!ok || generation != m_snapshot->generation)
        return {};
    int values[4];
    for (int i = 0; i < 4; ++i)
    {
        values[i] = parts[i + 1].toInt(&ok);
        if (!ok)
            return {};
    }
    auto image = windowMoveStrip(*m_snapshot, QRect(values[0], values[1], values[2], values[3]));
    if (size)
        *size = image.size();
    return image;
}
