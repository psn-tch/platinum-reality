#pragma once
#include <QHash>
#include <QImage>
#include <QMutex>
#include <QQuickImageProvider>
#include <memory>

// Images that services hand to QML by key (status item icons, notification images). The
// provider may be called from QML's image loading thread, so access is locked.
class IconStore final
{
  public:
    void set(const QString &key, const QImage &image);
    void remove(const QString &key);
    QImage image(const QString &key) const;
    // An image from the icon theme or a file path, at the given device-pixel size.
    static QImage resolve(const QString &nameOrPath, int size, const QStringList &extraPaths = {});

  private:
    mutable QMutex m_lock;
    QHash<QString, QImage> m_images;
};

class IconStoreProvider final : public QQuickImageProvider
{
  public:
    explicit IconStoreProvider(std::shared_ptr<IconStore> store)
        : QQuickImageProvider(QQuickImageProvider::Image), m_store(std::move(store))
    {
    }
    QImage requestImage(const QString &id, QSize *size, const QSize &requested) override;

  private:
    std::shared_ptr<IconStore> m_store;
};
