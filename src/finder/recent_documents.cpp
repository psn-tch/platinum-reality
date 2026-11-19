#include "recent_documents.hpp"
#include "favorites.hpp"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QVariantMap>
#include <algorithm>

namespace
{
constexpr qint64 maximumHistoryBytes = 1024 * 1024;
QVariantList history(const QString &profile)
{
    QFile file(profile + "/recent-documents.json");
    if (!file.open(QIODevice::ReadOnly))
        return {};
    const auto bytes = file.read(maximumHistoryBytes + 1);
    if (bytes.size() > maximumHistoryBytes)
        return {};
    QStringList paths;
    QVariantList records;
    for (const auto &value : QJsonDocument::fromJson(bytes).array())
    {
        if (paths.size() >= RecentDocuments::maximumCount)
            break;
        const auto record = value.toObject().toVariantMap();
        const auto path = record.value("target").toString(), alias = record.value("alias").toString();
        if (path.size() <= 4096 && !path.contains(QChar::Null) && QDir::isAbsolutePath(path) &&
            !paths.contains(path) && !alias.isEmpty() && alias != "." && alias != ".." &&
            !alias.contains('/') && !alias.contains(QChar::Null) && alias.size() <= 255 &&
            record.value("managed").typeId() == QMetaType::Bool)
        {
            paths.append(path);
            records.append(record);
        }
    }
    return records;
}
bool save(const QString &profile, const QVariantList &entries)
{
    QJsonArray array;
    for (const auto &value : entries)
    {
        const auto entry = value.toMap();
        array.append(QJsonObject::fromVariantMap({{"target", entry.value("target")},
                     {"alias", QFileInfo(entry.value("link").toString()).fileName()},
                     {"managed", entry.value("managed")}}));
    }
    const auto bytes = QJsonDocument(array).toJson(QJsonDocument::Compact);
    if (bytes.size() > maximumHistoryBytes || !QDir().mkpath(profile))
        return false;
    QSaveFile file(profile + "/recent-documents.json");
    return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size() && file.commit();
}
void removeAlias(const QVariantMap &entry)
{
    const QFileInfo alias(entry.value("link").toString());
    if (entry.value("managed").toBool() && alias.isSymLink() &&
        alias.symLinkTarget() == entry.value("target").toString())
        QFile::remove(alias.absoluteFilePath());
}
}

namespace RecentDocuments
{
QString folder(const QString &profile) { return profile + "/Recent Documents"; }
QVariantList list(const QString &profile)
{
    QVariantList result;
    for (const auto &value : history(profile))
    {
        auto entry = value.toMap();
        const auto path = entry.value("target").toString();
        const QFileInfo alias(folder(profile) + "/" + entry.value("alias").toString()), item(path);
        if (!alias.isSymLink() || alias.symLinkTarget() != path)
            continue; // Removing an alias in the Finder forgets the document too.
        if (item.isDir())
            continue;
        entry.insert("name", item.fileName());
        entry.insert("link", alias.absoluteFilePath());
        entry.insert("folder", false);
        entry.insert("exists", item.exists());
        result.append(entry);
    }
    return result;
}
bool remember(const QString &profile, const QString &document, int maximum)
{
    const QFileInfo item(document);
    const auto target = item.canonicalFilePath();
    const auto where = folder(profile);
    if (maximum < 1 || maximum > maximumCount || !item.isFile() || target.isEmpty() ||
        target.size() > 4096 || QFileInfo(where).isSymLink())
        return false;
    const auto before = list(profile);
    QVariantMap alias;
    for (const auto &entry : before)
        if (entry.toMap().value("target").toString() == target)
            alias = entry.toMap();
    if (alias.isEmpty())
        for (const auto &entry : Aliases::list(where))
            if (entry.toMap().value("target").toString() == target)
            {
                alias = entry.toMap();
                alias.insert("managed", false); // A user-created alias must survive Clear Menu.
                break;
            }
    const bool added = alias.isEmpty();
    if (added)
    {
        // One spare slot lets a full history accept a new document before trimming its oldest.
        if (!Aliases::add(where, QStringList{target}, maximumCount + 1))
            return false;
        for (const auto &entry : Aliases::list(where))
            if (entry.toMap().value("target").toString() == target)
                alias = entry.toMap();
        if (!alias.isEmpty())
            alias.insert("managed", true);
    }
    if (alias.isEmpty())
        return false;
    QVariantList next{alias};
    for (const auto &entry : before)
    {
        const auto path = entry.toMap().value("target").toString();
        if (path != target && next.size() < maximum)
            next.append(entry);
    }
    if (!save(profile, next))
    {
        if (added)
            removeAlias(alias);
        return false;
    }
    for (const auto &entry : before)
        if (std::none_of(next.cbegin(), next.cend(), [&](const QVariant &kept) {
                return kept.toMap().value("target") == entry.toMap().value("target"); }))
            removeAlias(entry.toMap());
    return true;
}
bool clear(const QString &profile)
{
    if (QFileInfo(folder(profile)).isSymLink())
        return false;
    const auto before = list(profile);
    if (!save(profile, {}))
        return false;
    for (const auto &entry : before)
        removeAlias(entry.toMap());
    return true;
}
}
