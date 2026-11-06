#include "appearance_settings.hpp"
#include <QDateTime>
#include <QGuiApplication>
#include <QStyleHints>
#include <array>
#include <tuple>
#include <QTimeZone>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QImageReader>
#include <QRegularExpression>
#include <QJsonDocument>
#include <QJsonObject>
#include <utility>
#include <QSaveFile>
#include <QStandardPaths>
#include <algorithm>

namespace
{
// Appearance → Highlight Color: the colour names and their light highlight (black text on it),
// with Black & White's black (white text) first, as the 9.2.2 menu lists them.
const QList<std::pair<QString, QString>> &highlights()
{
    static const QList<std::pair<QString, QString>> list{
        {"Black & White", "#000000"}, {"Blue", "#99ccff"}, {"Bronze", "#ffcc99"}, {"Gold", "#ffee88"},
        {"Graphite", "#cccccc"},      {"Green", "#ccffcc"}, {"Lavender", "#e6ccff"}, {"Purple", "#ccccff"},
        {"Red", "#ffcccc"},           {"Silver", "#e0e0e0"}};
    return list;
}
QString highlightValue(const QString &name)
{
    for (const auto &[label, value] : highlights())
        if (label == name)
            return value;
    return {};
}
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
    if (const auto name = object.value("highlight").toString(); !highlightValue(name).isEmpty())
        m_highlight = name;
    if (object.value("doubleClickCollapse").isBool())
        m_doubleClickCollapse = object.value("doubleClickCollapse").toBool();
    if (object.value("keepScreenAwake").isBool())
        m_keepScreenAwake = object.value("keepScreenAwake").toBool();
    if (const auto modules = object.value("stripModules").toInt(-1); modules >= 0 && modules <= 256)
        m_stripModules = modules;
    const auto clock = object.value("clock").toObject();
    for (auto it = m_clock.begin(); it != m_clock.end(); ++it)
        if (clock.value(it.key()).isBool())
            it.value() = clock.value(it.key()).toBool();
    const auto general = object.value("general").toObject();
    if (general.value("showDesktop").isBool())
        m_showDesktop = general.value("showDesktop").toBool();
    if (const auto blinks = general.value("menuBlinks").toInt(-1); blinks >= 0 && blinks <= 3)
        m_menuBlinks = blinks;
    if (const auto caret = general.value("caretBlink").toInt(-1); caret >= 0 && caret <= 2)
        m_caretBlink = caret;
    const auto switcher = object.value("switcher").toObject();
    if (const auto x = switcher.value("x").toInt(-1), y = switcher.value("y").toInt(-1);
        switcher.value("visible").isBool() && x >= 0 && x <= 65536 && y >= 0 && y <= 65536)
    {
        m_switcherVisible = switcher.value("visible").toBool();
        m_switcherX = x;
        m_switcherY = y;
    }
    applyCaretFlash();
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
    QJsonObject clock;
    for (auto it = m_clock.cbegin(); it != m_clock.cend(); ++it)
        clock.insert(it.key(), it.value());
    const auto bytes = QJsonDocument(QJsonObject{{"version", 1},
                                                 {"shadeDuration", duration},
                                                 {"stripVisible", visible},
                                                 {"stripCollapsed", collapsed},
                                                 {"stripX", x},
                                                 {"stripY", y},
                                                 {"stripModules", m_stripModules},
                                                 {"wallpaperPath", picture},
                                                 {"wallpaperLayout", layout},
                                                 {"accent", m_accent},
                                                 {"highlight", m_highlight},
                                                 {"doubleClickCollapse", m_doubleClickCollapse},
                                                 {"keepScreenAwake", m_keepScreenAwake},
                                                 {"clock", clock},
                                                 {"general", QJsonObject{{"showDesktop", m_showDesktop},
                                                                         {"menuBlinks", m_menuBlinks},
                                                                         {"caretBlink", m_caretBlink}}},
                                                 {"switcher", QJsonObject{{"visible", m_switcherVisible},
                                                                          {"x", m_switcherX},
                                                                          {"y", m_switcherY}}}})
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
QVariantMap AppearanceSettings::clock() const
{
    QVariantMap result;
    for (auto it = m_clock.cbegin(); it != m_clock.cend(); ++it)
        result.insert(it.key(), it.value());
    return result;
}
QVariantMap AppearanceSettings::timeZone() const
{
    const auto zone = QTimeZone::systemTimeZone();
    const auto now = QDateTime::currentDateTime();
    const auto seconds = zone.offsetFromUtc(now);
    const auto minutes = std::abs(seconds) / 60;
    auto amount = QString::number(minutes / 60) + (minutes / 60 == 1 ? " hour" : " hours");
    if (minutes % 60)
        amount += QString(" %1 minutes").arg(minutes % 60);
    return {{"id", QString::fromUtf8(zone.id())},
            {"name", zone.abbreviation(now)},
            {"offset", seconds == 0 ? QString("Same as UTC")
                                    : amount + (seconds < 0 ? " behind UTC" : " ahead of UTC")}};
}
bool AppearanceSettings::setClockOption(QString key, bool on)
{
    if (!m_clock.contains(key))
    {
        emit errorOccurred("That clock setting is not available.");
        return false;
    }
    if (m_clock.value(key) == on)
        return true;
    const auto previous = std::exchange(m_clock[key], on);
    if (save(m_duration, m_stripVisible, m_stripCollapsed, m_stripX, m_stripY, m_wallpaperPath,
             m_wallpaperLayout))
        return true;
    m_clock[key] = previous;
    return false;
}
QVariantMap AppearanceSettings::general() const
{
    return {{"showDesktop", m_showDesktop}, {"menuBlinks", m_menuBlinks}, {"caretBlink", m_caretBlink}};
}
QColor AppearanceSettings::highlightColor() const
{
    return QColor(highlightValue(m_highlight));
}
QColor AppearanceSettings::highlightTextColor() const
{
    return m_highlight == "Black & White" ? QColor(Qt::white) : QColor(Qt::black);
}
QVariantList AppearanceSettings::highlightChoices() const
{
    QVariantList result;
    for (const auto &[label, value] : highlights())
        result.append(QVariantMap{{"name", label}, {"color", value}});
    return result;
}
bool AppearanceSettings::setHighlight(QString name)
{
    if (highlightValue(name).isEmpty())
    {
        emit errorOccurred("That highlight colour is not available.");
        return false;
    }
    if (name == m_highlight)
        return true;
    const auto previous = std::exchange(m_highlight, name);
    if (save(m_duration, m_stripVisible, m_stripCollapsed, m_stripX, m_stripY, m_wallpaperPath,
             m_wallpaperLayout))
        return true;
    m_highlight = previous;
    return false;
}
QVariantMap AppearanceSettings::switcher() const
{
    return {{"visible", m_switcherVisible}, {"x", m_switcherX}, {"y", m_switcherY}};
}
bool AppearanceSettings::setSwitcher(bool visible, int x, int y)
{
    if (x < 0 || x > 65536 || y < 0 || y > 65536)
    {
        emit errorOccurred("The appearance setting is outside its supported range.");
        return false;
    }
    const auto previous = std::tuple{m_switcherVisible, m_switcherX, m_switcherY};
    std::tie(m_switcherVisible, m_switcherX, m_switcherY) = std::tuple{visible, x, y};
    if (save(m_duration, m_stripVisible, m_stripCollapsed, m_stripX, m_stripY, m_wallpaperPath,
             m_wallpaperLayout))
        return true;
    std::tie(m_switcherVisible, m_switcherX, m_switcherY) = previous;
    return false;
}
int AppearanceSettings::caretFlashTime() const
{
    // Slow, medium (Qt's own default) and fast.
    return std::array{1600, 1000, 500}[std::clamp(m_caretBlink, 0, 2)];
}
void AppearanceSettings::applyCaretFlash() const
{
    // Every text field of the desktop's own windows; applications keep their toolkit's setting.
    if (auto *application = qobject_cast<QGuiApplication *>(QCoreApplication::instance()))
        application->styleHints()->setCursorFlashTime(caretFlashTime());
}
bool AppearanceSettings::setGeneralOption(QString key, QVariant value)
{
    const auto number = value.toInt();
    const bool valid = key == "showDesktop" ? value.typeId() == QMetaType::Bool
                     : (key == "menuBlinks" || key == "caretBlink") && value.typeId() != QMetaType::Bool &&
                           value.canConvert<int>() && value.toDouble() == number &&
                           number >= 0 && number <= (key == "menuBlinks" ? 3 : 2);
    if (!valid)
    {
        emit errorOccurred("That General Controls setting is not available.");
        return false;
    }
    auto &slot = key == "menuBlinks" ? m_menuBlinks : m_caretBlink;
    const auto previous = std::tuple{m_showDesktop, m_menuBlinks, m_caretBlink};
    if (key == "showDesktop")
        m_showDesktop = value.toBool();
    else
        slot = number;
    if (save(m_duration, m_stripVisible, m_stripCollapsed, m_stripX, m_stripY, m_wallpaperPath,
             m_wallpaperLayout))
    {
        applyCaretFlash();
        return true;
    }
    std::tie(m_showDesktop, m_menuBlinks, m_caretBlink) = previous;
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
bool AppearanceSettings::setControlStripModules(int modules)
{
    if (modules < 0 || modules > 256)
    {
        emit errorOccurred("The appearance setting is outside its supported range.");
        return false;
    }
    const auto previous = std::exchange(m_stripModules, modules);
    if (save(m_duration, m_stripVisible, false, m_stripX, m_stripY, m_wallpaperPath,
             m_wallpaperLayout))
        return true;
    m_stripModules = previous;
    return false;
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
namespace
{
const QRegularExpression &patternForm()
{
    static const QRegularExpression form("^[0-9a-f]{16}$");
    return form;
}
} // namespace
QString AppearanceSettings::desktopPattern() const
{
    // Only a pattern this panel wrote, still in use: patterns/<16 hex digits>.png, tiled.
    const QFileInfo file(m_wallpaperPath);
    if (m_wallpaperLayout != "tile" || file.absolutePath() != QDir(m_profile).filePath("patterns") ||
        file.suffix() != "png" || !patternForm().match(file.completeBaseName()).hasMatch())
        return {};
    return file.completeBaseName();
}
bool AppearanceSettings::setDesktopPattern(QString pattern)
{
    pattern = pattern.toLower();
    if (!patternForm().match(pattern).hasMatch())
    {
        emit errorOccurred("A desktop pattern is 8 rows of 8 dots.");
        return false;
    }
    const QDir folder(QDir(m_profile).filePath("patterns"));
    const auto path = folder.filePath(pattern + ".png");
    if (!QDir().mkpath(folder.path()) || QFileInfo(folder.path()).isSymLink())
    {
        emit errorOccurred("The desktop pattern could not be saved.");
        return false;
    }
    QImage tile(8, 8, QImage::Format_Mono);
    tile.setColorTable({qRgb(255, 255, 255), qRgb(0, 0, 0)});
    for (int y = 0; y < 8; ++y)
    {
        const auto row = pattern.mid(2 * y, 2).toUInt(nullptr, 16);
        for (int x = 0; x < 8; ++x)
            tile.setPixel(x, y, (row >> (7 - x)) & 1);
    }
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly) || !tile.save(&file, "PNG") || !file.commit())
    {
        emit errorOccurred("The desktop pattern could not be saved: " + file.errorString());
        return false;
    }
    const auto previous = desktopPattern();
    if (!applyDesktopPicture(path, "tile"))
        return false;
    if (!previous.isEmpty() && previous != pattern)
        QFile::remove(folder.filePath(previous + ".png"));
    return true;
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
