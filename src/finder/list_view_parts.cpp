#include "list_view_parts.hpp"
#include <QPixmapCache>

QIcon selectedFolderIcon(const QIcon &icon, QSize size, qreal ratio)
{
    // Raster icons can carry explicit state artwork. Named theme engines may
    // report sizes for generated states too; retain our default for those.
    // Require this logical size, so a 32px state cannot leak into the 16px view.
    if (icon.name().isEmpty() && icon.availableSizes(QIcon::Selected).contains(size))
        // Isolate the logical slot before Qt scales for the target display.
        // Requesting at ratio>1 here could choose the other slot's artwork.
        return QIcon(icon.pixmap(size,1.0,QIcon::Selected));
    // Measured list icons, Sherlock and the ordinary folder halve RGB. Some
    // reference artwork uses a different mapping; do not apply it universally
    // without source-family provenance (see docs/fidelity/desktop-icons.md).
    const auto key = QString("platinum-finder-selected-rgb-half-%1-%2x%3-%4")
        .arg(icon.cacheKey()).arg(size.width()).arg(size.height()).arg(ratio);
    QPixmap pixmap;
    if (!QPixmapCache::find(key,&pixmap))
    {
        auto image = icon.pixmap(size,ratio,QIcon::Normal).toImage().convertToFormat(QImage::Format_ARGB32);
        if (image.isNull()) return icon;
        for (int y=0;y<image.height();++y)
        {
            auto *pixels=reinterpret_cast<QRgb*>(image.scanLine(y));
            for (int x=0;x<image.width();++x)
            {
                const auto pixel=pixels[x];
                pixels[x]=qRgba(qRed(pixel)/2,qGreen(pixel)/2,qBlue(pixel)/2,qAlpha(pixel));
            }
        }
        pixmap=QPixmap::fromImage(image);
        QPixmapCache::insert(key,pixmap);
    }
    return QIcon(pixmap);
}
