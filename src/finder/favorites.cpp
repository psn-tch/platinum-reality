#include "favorites.hpp"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QVariantMap>

#include <algorithm>

namespace Aliases
{
QVariantList list(const QString &folder)
{
    QVariantList result;
    for (const auto &link : QDir(folder).entryInfoList(QDir::AllEntries | QDir::System | QDir::NoDotAndDotDot,
                                                       QDir::Name | QDir::IgnoreCase))
    {
        if (!link.isSymLink())
            continue;
        const auto target = link.symLinkTarget();
        const QFileInfo item(target);
        result.append(QVariantMap{{"name", link.fileName()}, {"link", link.absoluteFilePath()}, {"target", target},
                                  {"folder", item.isDir()}, {"exists", item.exists()}});
    }
    return result;
}

int add(const QString &where, const QList<std::pair<QString, QString>> &items, int maximum)
{
    if (!QDir().mkpath(where))
        return 0;
    const auto existing = list(where);
    int count = int(existing.size()), added = 0;
    for (const auto &[path, givenName] : items)
    {
        const QFileInfo item(path);
        const auto target = item.absoluteFilePath();
        if (!item.exists() || count >= maximum ||
            std::any_of(existing.cbegin(), existing.cend(),
                        [&](const QVariant &f) { return f.toMap().value("target").toString() == target; }))
            continue;
        // A name is a single file name: never a path, never hidden, also for a dot file's own name
        // (a hidden alias is never listed, so it was added again each time).
        const auto plain = [](QString name)
        {
            name = name.replace('/', '-').trimmed();
            while (name.startsWith('.'))
                name.remove(0, 1);
            return name;
        };
        auto base = plain(givenName);
        if (base.isEmpty())
            base = plain(item.fileName());
        if (base.isEmpty())
            base = item.fileName().isEmpty() ? QString("Hard Disk") : QString("Alias");
        auto name = base;
        for (int n = 2; QFileInfo(where + "/" + name).exists() || QFileInfo(where + "/" + name).isSymLink(); ++n)
            name = base + " " + QString::number(n);
        if (QFile::link(target, where + "/" + name))
        {
            ++added;
            ++count;
        }
    }
    return added;
}

int add(const QString &where, const QStringList &items, int maximum)
{
    QList<std::pair<QString, QString>> named;
    for (const auto &path : items)
        named.append({path, QString()});
    return add(where, named, maximum);
}
} // namespace Aliases

namespace Favorites
{
QString folder(const QString &profile) { return profile + "/Favorites"; }
int add(const QString &profile, const QStringList &items) { return Aliases::add(folder(profile), items, maximumCount); }
QVariantList list(const QString &profile) { return Aliases::list(folder(profile)); }
} // namespace Favorites

namespace LauncherItems
{
QString folder(const QString &profile) { return profile + "/Launcher Items"; }
int add(const QString &profile, const QStringList &items) { return Aliases::add(folder(profile), items, maximumCount); }
bool addApplication(const QString &profile, const QString &file, const QString &name)
{
    return Aliases::add(folder(profile), QList<std::pair<QString, QString>>{{file, name}}, maximumCount) == 1;
}
} // namespace LauncherItems
