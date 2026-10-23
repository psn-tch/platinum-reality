#include "appearance_settings.hpp"
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QImageReader>
#include <QJsonDocument>
#include <QJsonObject>
#include <utility>
#include <QSaveFile>
#include <QStandardPaths>
#include <algorithm>

namespace
{
bool valid(int duration, int x, int y)
{
    return duration >= 0 && duration <= 1200 && x >= 0 && x <= 65536 && y >= -1 && y <= 65536;
}
QUrl localEmblem(const QString &profile)
{
    const QStringList candidates{
        profile + "/assets/apple-menu.png",
        QStandardPaths::locate(QStandardPaths::GenericDataLocation,
                               "icons/NineIcons/places/16/distributor-logo.png")};
    for (const auto &path : candidates)
    {
        if (path.isEmpty() || QFileInfo(path).size() > 1024 * 1024)
            continue;
        QImageReader reader(path, "png");
        const auto size = reader.size();
        if (reader.canRead() && size.width() > 0 && size.height() > 0 && size.width() <= 512 &&
            size.height() <= 512)
            return QUrl::fromLocalFile(QFileInfo(path).absoluteFilePath());
    }
    return {};
}
QString absolutePicturePath(QString path)
{
    if (path.startsWith("~/"))
        path = QDir::homePath() + path.mid(1);
    return QDir::isAbsolutePath(path) && !path.contains(QChar::Null) ? QDir::cleanPath(path)
                                                                     : QString();
}
bool validLayout(const QString &layout)
{
    return layout == "fill" || layout == "fit" || layout == "center" || layout == "tile";
}
QUrl pictureUrl(const QString &path, bool decode = false)
{
    if (path.isEmpty())
        return {};
    const QFileInfo file(path);
    if (!file.isFile() || !file.isReadable() || file.size() <= 0 || file.size() > 64 * 1024 * 1024)
        return {};
    QImageReader reader(path);
    const auto format = reader.format();
    const auto size = reader.size();
    if ((format != "png" && format != "jpeg" && format != "webp") || !reader.canRead() ||
        size.width() <= 0 || size.height() <= 0 ||
        qint64(size.width()) * size.height() > 32 * 1024 * 1024)
        return {};
    if (decode)
    {
        reader.setScaledSize(size.scaled(512, 512, Qt::KeepAspectRatio));
        if (reader.read().isNull())
            return {};
    }
    return QUrl::fromLocalFile(file.absoluteFilePath());
}
} // namespace
AppearanceSettings::AppearanceSettings(QString profile, QObject *parent)
    : QObject(parent), m_profile(QDir(profile).absolutePath()), m_emblem(localEmblem(m_profile)),
      m_wallpaperPath(defaultWallpaperPath())
{
    m_wallpaper = pictureUrl(m_wallpaperPath);
    QFile input(m_profile + "/appearance.json");
    if (!input.open(QIODevice::ReadOnly) || input.size() > 16 * 1024)
        return;
    const auto bytes = input.read(16 * 1024 + 1);
    if (bytes.size() > 16 * 1024)
        return;
    const auto object = QJsonDocument::fromJson(bytes).object();
    const auto duration = object.value("shadeDuration").toInt(-1),
               x = object.value("stripX").toInt(-1), y = object.value("stripY").toInt(-2);
    if (object.value("version").toInt() != 1 || !valid(duration, x, y) ||
        !object.value("stripVisible").isBool() || !object.value("stripCollapsed").isBool())
        return;
    m_duration = duration;
    m_stripX = x;
    m_stripY = y;
    m_stripVisible = object.value("stripVisible").toBool();
    m_stripCollapsed = object.value("stripCollapsed").toBool();
    // Optional fields extend version 1 without invalidating existing profiles.
    if (object.value("wallpaperPath").isString() && object.value("wallpaperLayout").isString())
    {
        const auto path = object.value("wallpaperPath").toString();
        const auto layout = object.value("wallpaperLayout").toString();
        if (validLayout(layout) && (path.isEmpty() || !absolutePicturePath(path).isEmpty()))
        {
            m_wallpaperPath = path.isEmpty() ? QString() : absolutePicturePath(path);
            m_wallpaperLayout = layout;
            // Missing pictures use the solid desktop; retain the user's path.
            m_wallpaper = pictureUrl(m_wallpaperPath);
        }
    }
    if (const auto accent = object.value("accent").toString(); accent == "blue" || accent == "graphite")
        m_accent = accent;
    if (object.value("doubleClickCollapse").isBool())
        m_doubleClickCollapse = object.value("doubleClickCollapse").toBool();
    if (object.value("keepScreenAwake").isBool())
        m_keepScreenAwake = object.value("keepScreenAwake").toBool();
}
bool AppearanceSettings::save(int duration, bool visible, bool collapsed, int x, int y,
                              const QString &picture, const QString &layout)
{
    if (!valid(duration, x, y))
    {
        emit errorOccurred("The appearance setting is outside its supported range.");
        return false;
    }
    if (!QDir().mkpath(m_profile))
    {
        emit errorOccurred("The appearance settings folder could not be created.");
        return false;
    }
    const auto bytes = QJsonDocument(QJsonObject{{"version", 1},
                                                 {"shadeDuration", duration},
                                                 {"stripVisible", visible},
                                                 {"stripCollapsed", collapsed},
                                                 {"stripX", x},
                                                 {"stripY", y},
                                                 {"wallpaperPath", picture},
                                                 {"wallpaperLayout", layout},
                                                 {"accent", m_accent},
                                                 {"doubleClickCollapse", m_doubleClickCollapse},
                                                 {"keepScreenAwake", m_keepScreenAwake}})
                           .toJson();
    QSaveFile file(m_profile + "/appearance.json");
    if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() || !file.commit())
    {
        emit errorOccurred("Appearance settings could not be saved: " + file.errorString());
        return false;
    }
    m_duration = duration;
    m_stripVisible = visible;
    m_stripCollapsed = collapsed;
    m_stripX = x;
    m_stripY = y;
    m_wallpaperPath = picture;
    m_wallpaperLayout = layout;
    m_wallpaper = pictureUrl(picture);
    emit changed();
    return true;
}
bool AppearanceSettings::apply(int duration, bool visible)
{
    return save(duration, visible, m_stripCollapsed, m_stripX, m_stripY, m_wallpaperPath,
                m_wallpaperLayout);
}
QColor AppearanceSettings::accentColor() const
{
    // Sampled from 9.2 captures (docs/historical-fidelity-review.md F01; compression-approximate).
    return QColor(m_accent == "graphite" ? "#656565" : "#2c2994");
}
bool AppearanceSettings::applyOptions(QString accent, bool doubleClickCollapse)
{
    if (accent != "blue" && accent != "graphite")
    {
        emit errorOccurred("That appearance variation is not available.");
        return false;
    }
    const auto previousAccent = std::exchange(m_accent, accent);
    const auto previousCollapse = std::exchange(m_doubleClickCollapse, doubleClickCollapse);
    if (save(m_duration, m_stripVisible, m_stripCollapsed, m_stripX, m_stripY, m_wallpaperPath,
             m_wallpaperLayout))
        return true;
    m_accent = previousAccent;
    m_doubleClickCollapse = previousCollapse;
    return false;
}
bool AppearanceSettings::setKeepScreenAwake(bool keepAwake)
{
    if (keepAwake == m_keepScreenAwake)
        return true;
    const auto previous = std::exchange(m_keepScreenAwake, keepAwake);
    if (save(m_duration, m_stripVisible, m_stripCollapsed, m_stripX, m_stripY, m_wallpaperPath,
             m_wallpaperLayout))
        return true;
    m_keepScreenAwake = previous;
    return false;
}
bool AppearanceSettings::setControlStripCollapsed(bool collapsed)
{
    return save(m_duration, m_stripVisible, collapsed, m_stripX, m_stripY, m_wallpaperPath,
                m_wallpaperLayout);
}
bool AppearanceSettings::setControlStripPosition(int x, int y)
{
    return save(m_duration, m_stripVisible, m_stripCollapsed, x, y, m_wallpaperPath,
                m_wallpaperLayout);
}
QString AppearanceSettings::picturesFolder() const
{
    // The user's Pictures folder (XDG), else home. A saved picture keeps its own folder.
    const auto pictures = QStandardPaths::writableLocation(QStandardPaths::PicturesLocation);
    return !pictures.isEmpty() && QFileInfo(pictures).isDir() ? pictures : QDir::homePath();
}
QString AppearanceSettings::defaultWallpaperPath() const
{
    // No picture: the plain Platinum desktop. (Until 2026-10-22 this was the project owner's own
    // ~/wallpaper/another1.png; profiles that chose it keep it, as appearance.json names it.)
    return {};
}
QUrl AppearanceSettings::previewDesktopPicture(QString path) const
{
    return pictureUrl(absolutePicturePath(path));
}
bool AppearanceSettings::applyDesktopPicture(QString path, QString layout)
{
    const auto normalized = absolutePicturePath(path);
    if (!validLayout(layout) ||
        (!path.isEmpty() && (normalized.isEmpty() || pictureUrl(normalized, true).isEmpty())))
    {
        emit errorOccurred(
            "Choose a readable PNG, JPEG or WebP picture (up to 64 MB and 32 megapixels).");
        return false;
    }
    return save(m_duration, m_stripVisible, m_stripCollapsed, m_stripX, m_stripY, normalized,
                layout);
}
QVariantMap AppearanceSettings::listDesktopPictures(QString folder) const
{
    const auto path = absolutePicturePath(folder);
    const QFileInfo info(path);
    if (path.isEmpty() || !info.isDir() || !info.isReadable())
        return {{"files", QVariantList{}}, {"notice", "Enter a readable picture folder."}};
    QVariantList files;
    QDirIterator entries(path, QDir::Files | QDir::NoDotAndDotDot | QDir::Readable);
    int examined = 0;
    while (entries.hasNext() && examined < 4096 && files.size() < 128)
    {
        const auto entry = entries.next();
        ++examined;
        const auto url = pictureUrl(entry);
        if (!url.isEmpty())
            files.push_back(
                QVariantMap{{"name", entries.fileName()}, {"path", entry}, {"url", url}});
    }
    std::sort(files.begin(), files.end(),
              [](const QVariant &a, const QVariant &b)
              {
                  return a.toMap().value("name").toString().compare(
                             b.toMap().value("name").toString(), Qt::CaseInsensitive) < 0;
              });
    return {{"files", files},
            {"folder", path},
            {"notice", entries.hasNext()
                           ? "Showing a limited selection; enter a picture's full path to use it."
                       : files.isEmpty() ? "No PNG, JPEG or WebP pictures in this folder."
                                         : "Choose a picture or enter its full path."}};
}

QVariantList AppearanceSettings::pictureCollections(QString root) const
{
    QVariantList collections;
    const QFileInfo base(root);
    if (root.isEmpty() || !QDir::isAbsolutePath(root) || !base.isDir() || base.isSymLink())
        return collections;
    const auto entries = QDir(root).entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot | QDir::Readable,
                                                  QDir::Name | QDir::IgnoreCase);
    for (const auto &entry : entries)
    {
        if (collections.size() >= 32)
            break;
        if (!entry.isSymLink())
            collections.push_back(QVariantMap{{"name", entry.fileName().left(80)}, {"path", entry.absoluteFilePath()}});
    }
    return collections;
}
