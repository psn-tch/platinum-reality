#include "startup_items.hpp"
#include "applications/desktop_entry.hpp"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QRegularExpression>
#include <QSaveFile>
#include <QStandardPaths>
#include <algorithm>

namespace
{
bool intersects(const QStringList &a, const QStringList &b)
{
    return std::any_of(a.begin(), a.end(), [&](const QString &name) { return b.contains(name); });
}
} // namespace

StartupItems::StartupItems(QString profile, QStringList directories, QStringList desktopNames,
                           QProcessEnvironment environment, QObject *parent)
    : QObject(parent), m_profile(std::move(profile)), m_directories(std::move(directories)),
      m_desktopNames(std::move(desktopNames)), m_environment(std::move(environment))
{
    const bool firstSession = !QFileInfo::exists(m_profile + "/extensions.json");
    load();
    refresh();
    if (firstSession)
    {
        // The items already installed belong to other desktops (KDE's tray applets, Steam, kwallet's
        // PAM helper...), and Platinum Dreams never started them before: they begin Off, so the first
        // login with Extensions Manager starts nothing new. Items installed later are On, as a newly
        // installed extension was in 9.2.2 (Cotato, 2026-11-14).
        auto &off = m_sets[mySettings];
        for (const auto &item : std::as_const(m_items))
            off.append(item.id);
        if (save())
            load(); // What Revert returns to.
    }
    m_startedAtLogin = startingIds();
    connect(&m_pacman, &QProcess::finished, this, &StartupItems::readPackages);
}
StartupItems::~StartupItems()
{
    if (m_pacman.state() != QProcess::NotRunning)
    {
        m_pacman.kill();
        m_pacman.waitForFinished(1000);
    }
}
QStringList StartupItems::standardDirectories(const QProcessEnvironment &environment)
{
    auto home = environment.value("XDG_CONFIG_HOME");
    if (!QDir::isAbsolutePath(home))
        home = environment.value("HOME", QDir::homePath()) + "/.config";
    QStringList result{home + "/autostart"};
    auto system = environment.value("XDG_CONFIG_DIRS");
    if (system.isEmpty())
        system = "/etc/xdg";
    for (const auto &directory : system.split(':', Qt::SkipEmptyParts))
        if (QDir::isAbsolutePath(directory))
            result.append(directory + "/autostart");
    return result;
}
void StartupItems::load()
{
    m_sets.clear();
    m_selected = mySettings;
    m_sets.insert(mySettings, {});
    QFile file(m_profile + "/extensions.json");
    if (file.open(QIODevice::ReadOnly) && file.size() <= 256 * 1024)
    {
        const auto saved = QJsonDocument::fromJson(file.readAll()).object();
        m_atLogin = saved;
        auto sets = saved.value("sets").toObject();
        QHash<QString, QString> renamed;
        // These names were available for custom sets before the product rename. Keep those
        // choices under a unique name rather than discarding them in favour of the built-ins.
        for (const auto &key : sets.keys())
        {
            if (!locked(key))
                continue;
            auto custom = key + " (Custom)";
            for (int number = 2; sets.contains(custom); ++number)
                custom = key + " (Custom " + QString::number(number) + ")";
            sets.insert(custom, sets.take(key));
            renamed.insert(key, custom);
        }
        for (auto it = sets.begin(); it != sets.end() && m_sets.size() < 64; ++it)
        {
            if (locked(it.key()) || it.key().trimmed().isEmpty() || it.key().size() > 64)
                continue;
            QStringList off;
            for (const auto &id : it.value().toObject().value("off").toArray())
                if (id.isString() && off.size() < 4096)
                    off.append(id.toString());
            m_sets.insert(it.key(), off);
        }
        auto selected = saved.value("selected").toString();
        // Only built-in set names change; user-defined sets and their choices stay intact.
        if (renamed.contains(selected))
            selected = renamed.value(selected);
        else if (!m_sets.contains(selected) && selected == "Platinum Reality All")
            selected = allSet;
        else if (!m_sets.contains(selected) && selected == "Platinum Reality Base")
            selected = baseSet;
        if (m_sets.contains(selected) || locked(selected))
            m_selected = selected;
    }
}
bool StartupItems::save()
{
    QJsonObject sets;
    for (auto it = m_sets.begin(); it != m_sets.end(); ++it)
        sets.insert(it.key(), QJsonObject{{"off", QJsonArray::fromStringList(it.value())}});
    QSaveFile file(m_profile + "/extensions.json");
    if (!QDir().mkpath(m_profile) || !file.open(QIODevice::WriteOnly) ||
        file.write(QJsonDocument(QJsonObject{{"selected", m_selected}, {"sets", sets}}).toJson()) < 0 ||
        !file.commit())
    {
        m_error = "The startup items could not be saved.";
        emit changed();
        return false;
    }
    m_error.clear();
    emit changed();
    return true;
}
void StartupItems::refresh()
{
    QList<Item> found;
    QStringList seen;
    for (int place = 0; place < m_directories.size(); ++place)
    {
        const QDir directory(m_directories[place]);
        const auto names = directory.entryList({"*.desktop"}, QDir::Files | QDir::Readable, QDir::Name);
        for (const auto &name : names)
        {
            // A file in a more important folder hides the system's file of the same name, even
            // one that only says Hidden=true (the standard's way to turn a system item off).
            if (seen.contains(name))
                continue;
            seen.append(name);
            const QFileInfo info(directory.filePath(name));
            if (info.isSymLink() && !info.exists())
                continue;
            const auto entry = frankenstein::applications::readDesktopEntry(info.absoluteFilePath(), name, m_environment);
            if (!entry.unavailable.isEmpty() || entry.hidden || !entry.autostartEnabled || entry.tokens.isEmpty())
                continue;
            if ((!entry.onlyShowIn.isEmpty() && !intersects(entry.onlyShowIn, m_desktopNames)) ||
                intersects(entry.notShowIn, m_desktopNames))
                continue;
            Item item;
            item.id = name;
            item.name = entry.name;
            item.comment = entry.comment;
            item.file = info.absoluteFilePath();
            item.command = entry.tokens.join(' ');
            const auto first = entry.tokens.first();
            item.program = QDir::isAbsolutePath(first)
                               ? first
                               : QStandardPaths::findExecutable(first, m_environment.value("PATH").split(':', Qt::SkipEmptyParts));
            item.size = QFileInfo(item.program).size();
            item.user = place == 0;
            for (const auto &old : std::as_const(m_items))
                if (old.id == item.id && old.program == item.program)
                {
                    item.package = old.package;
                    item.version = old.version;
                }
            found.append(item);
        }
    }
    std::stable_sort(found.begin(), found.end(), [](const Item &a, const Item &b)
                     { return a.user != b.user ? a.user : a.name.compare(b.name, Qt::CaseInsensitive) < 0; });
    m_items = found;
    emit changed();
    // Which package each program comes from, for the Package and Version columns.
    if (m_pacman.state() != QProcess::NotRunning || m_environment.value("FRANKENSTEIN_NATIVE_TEST") == "1")
        return;
    const auto pacman = QStandardPaths::findExecutable("pacman");
    QStringList programs;
    for (const auto &item : std::as_const(m_items))
        if (!item.program.isEmpty() && !programs.contains(item.program))
            programs.append(item.program);
    if (pacman.isEmpty() || programs.isEmpty())
        return;
    auto environment = QProcessEnvironment::systemEnvironment();
    environment.insert("LC_ALL", "C");
    m_pacman.setProcessEnvironment(environment);
    m_pacman.start(pacman, QStringList{"-Qo", "--"} + programs);
}
void StartupItems::readPackages()
{
    static const QRegularExpression owned("^(/.+) is owned by (\\S+) (\\S+)$");
    QHash<QString, QPair<QString, QString>> packages;
    for (const auto &line : QString::fromUtf8(m_pacman.readAllStandardOutput()).split('\n'))
    {
        const auto match = owned.match(line.trimmed());
        if (match.hasMatch())
            packages.insert(match.captured(1), {match.captured(2), match.captured(3)});
    }
    for (auto &item : m_items)
        if (packages.contains(item.program))
        {
            item.package = packages[item.program].first;
            item.version = packages[item.program].second;
        }
    emit changed();
}
QStringList StartupItems::offIn(const QString &set) const
{
    if (set == baseSet)
    {
        QStringList all;
        for (const auto &item : m_items)
            all.append(item.id);
        return all;
    }
    return set == allSet ? QStringList() : m_sets.value(set);
}
bool StartupItems::isOn(const QString &id) const
{
    return !offIn(m_selected).contains(id);
}
QStringList StartupItems::startingIds() const
{
    QStringList ids;
    for (const auto &item : m_items)
        if (isOn(item.id))
            ids.append(item.id);
    return ids;
}
bool StartupItems::changedSinceLogin() const
{
    return startingIds() != m_startedAtLogin;
}
QVariantList StartupItems::items() const
{
    QVariantList result;
    for (const auto &item : m_items)
        result.append(QVariantMap{{"id", item.id}, {"name", item.name}, {"comment", item.comment},
                                  {"file", item.file}, {"command", item.command}, {"program", item.program},
                                  {"user", item.user}, {"size", item.size}, {"package", item.package},
                                  {"version", item.version}, {"on", isOn(item.id)}});
    return result;
}
QStringList StartupItems::sets() const
{
    auto names = m_sets.keys();
    names.removeAll(mySettings);
    names.sort(Qt::CaseInsensitive);
    return QStringList{mySettings} + names + QStringList{allSet, baseSet};
}
void StartupItems::unlockSelected()
{
    if (!locked(m_selected))
        return;
    m_sets.insert(mySettings, offIn(m_selected));
    m_selected = mySettings;
}
bool StartupItems::setOn(const QString &id, bool on)
{
    if (!std::any_of(m_items.begin(), m_items.end(), [&](const Item &item) { return item.id == id; }))
        return false;
    unlockSelected();
    auto &off = m_sets[m_selected];
    off.removeAll(id);
    if (!on)
        off.append(id);
    return save();
}
bool StartupItems::setGroupOn(bool user, bool on)
{
    unlockSelected();
    auto &off = m_sets[m_selected];
    for (const auto &item : std::as_const(m_items))
        if (item.user == user)
        {
            off.removeAll(item.id);
            if (!on)
                off.append(item.id);
        }
    return save();
}
bool StartupItems::selectSet(const QString &name)
{
    if (!m_sets.contains(name) && !locked(name))
        return false;
    m_selected = name;
    return save();
}
bool StartupItems::duplicateSet(const QString &name)
{
    const auto trimmed = name.trimmed();
    if (trimmed.isEmpty() || trimmed.size() > 64 || m_sets.contains(trimmed) || locked(trimmed) || m_sets.size() >= 64)
    {
        m_error = trimmed.isEmpty() ? "Type a name for the new set."
                                    : "There is already a set named “" + trimmed + "”.";
        emit changed();
        return false;
    }
    m_sets.insert(trimmed, offIn(m_selected));
    m_selected = trimmed;
    return save();
}
bool StartupItems::deleteSet(const QString &name)
{
    if (locked(name) || name == mySettings || !m_sets.remove(name))
        return false;
    if (m_selected == name)
        m_selected = mySettings;
    return save();
}
void StartupItems::revert()
{
    QSaveFile file(m_profile + "/extensions.json");
    if (m_atLogin.isEmpty())
        QFile::remove(m_profile + "/extensions.json");
    else if (!file.open(QIODevice::WriteOnly) || file.write(QJsonDocument(m_atLogin).toJson()) < 0 || !file.commit())
    {
        m_error = "The startup items could not be put back.";
        emit changed();
        return;
    }
    load();
    m_error.clear();
    emit changed();
}
QStringList StartupItems::startAll(const Launcher &launcher)
{
    QStringList started;
    for (const auto &item : std::as_const(m_items))
        if (isOn(item.id) && launcher(item.file, item.id))
            started.append(item.id);
    return started;
}
