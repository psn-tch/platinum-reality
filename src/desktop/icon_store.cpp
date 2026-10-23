#include "icon_store.hpp"
#include <QDirIterator>
#include <QSettings>
#include <QStandardPaths>
#include <QFileInfo>
#include <QIcon>
#include <QMutexLocker>
#include <QUrl>
#include <utility>

void IconStore::set(const QString &key, const QImage &image)
{
    QMutexLocker locker(&m_lock);
    m_images.insert(key, image);
}

void IconStore::remove(const QString &key)
{
    QMutexLocker locker(&m_lock);
    m_images.remove(key);
}

QImage IconStore::image(const QString &key) const
{
    QMutexLocker locker(&m_lock);
    return m_images.value(key);
}

namespace
{
// Outside a KDE session Qt only knows the bare hicolor theme, so names such as
// dialog-information resolve to nothing. Use the user's own KDE icon theme (read-only), then
// common complete themes; each theme's own Inherits chain still applies.
void ensureIconTheme()
{
    static bool done = false;
    if (std::exchange(done, true) || (!QIcon::themeName().isEmpty() && QIcon::themeName() != "hicolor"))
        return;
    QStringList candidates;
    const auto config = QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation);
    const QSettings kde(config + "/kdeglobals", QSettings::IniFormat);
    candidates << kde.value("Icons/Theme").toString() << "breeze" << "Adwaita";
    QStringList roots = QIcon::themeSearchPaths();
    roots << QDir::homePath() + "/.local/share/icons" << QDir::homePath() + "/.icons";
    for (const auto &theme : candidates)
    {
        if (theme.isEmpty() || theme.contains('/'))
            continue;
        for (const auto &root : roots)
            if (QFileInfo(root + '/' + theme + "/index.theme").isFile())
            {
                QIcon::setThemeSearchPaths(roots);
                QIcon::setThemeName(theme);
                QIcon::setFallbackThemeName("hicolor");
                return;
            }
    }
}
} // namespace

QImage IconStore::resolve(const QString &nameOrPath, int size, const QStringList &extraPaths)
{
    ensureIconTheme();
    if (nameOrPath.isEmpty() || nameOrPath.size() > 4096)
        return {};
    auto path = nameOrPath.startsWith("file://") ? QUrl(nameOrPath).toLocalFile() : nameOrPath;
    const auto draw = [size](const QIcon &icon) { return icon.isNull() ? QImage() : icon.pixmap(size, size).toImage(); };
    if (QFileInfo(path).isAbsolute())
        return QFileInfo(path).isFile() ? draw(QIcon(path)) : QImage();
    if (path.contains('/'))
        return {}; // Relative paths are neither files nor theme names.
    // Applications may ship their own icon directory (StatusNotifierItem IconThemePath).
    for (const auto &directory : extraPaths)
    {
        if (!QFileInfo(directory).isAbsolute())
            continue;
        QDirIterator files(directory, {path + ".png", path + ".svg", path + ".xpm"}, QDir::Files,
                           QDirIterator::Subdirectories);
        for (int checked = 0; files.hasNext() && checked < 64; ++checked)
            if (const auto image = draw(QIcon(files.next())); !image.isNull())
                return image;
    }
    return draw(QIcon::fromTheme(path));
}

QImage IconStoreProvider::requestImage(const QString &id, QSize *size, const QSize &requested)
{
    // Keys carry a revision after '@' so QML reloads changed icons.
    auto image = m_store->image(id.section('@', 0, 0));
    if (!image.isNull() && requested.isValid())
        image = image.scaled(requested, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    if (size)
        *size = image.size();
    return image;
}
