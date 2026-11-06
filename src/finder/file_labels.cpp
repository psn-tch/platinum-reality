#include "file_labels.hpp"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <cerrno>
#include <sys/xattr.h>

namespace
{
constexpr const char *attribute = "user.platinum.label";
constexpr qint64 maximumDatabase = 1024 * 1024;
constexpr int maximumEntries = 8192;
QString databasePath(const QString &profile)
{
    return QDir(profile).absoluteFilePath("labels.json");
}
QJsonObject readDatabase(const QString &profile)
{
    QFile file(databasePath(profile));
    if (profile.isEmpty() || QFileInfo(file.fileName()).isSymLink() || !file.open(QIODevice::ReadOnly) ||
        file.size() > maximumDatabase)
        return {};
    const auto object = QJsonDocument::fromJson(file.read(maximumDatabase + 1)).object();
    return object.value("version").toInt() == 1 ? object.value("labels").toObject() : QJsonObject();
}
bool writeDatabase(const QString &profile, const QJsonObject &labels)
{
    if (profile.isEmpty() || labels.size() > maximumEntries || !QDir().mkpath(profile))
        return false;
    const auto bytes = QJsonDocument(QJsonObject{{"version", 1}, {"labels", labels}}).toJson();
    QSaveFile file(databasePath(profile));
    return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size() && file.commit();
}
bool unsupported(int error)
{
    return error == ENOTSUP || error == EOPNOTSUPP || error == EPERM;
}
} // namespace

namespace FileLabels
{
const QList<Label> &labels()
{
    static const QList<Label> list{{1, "Orange", "#ff6403"}, {2, "Red", "#dd0907"},
                                   {3, "Pink", "#f20884"},   {4, "Light Blue", "#02abea"},
                                   {5, "Blue", "#0000d3"},   {6, "Green", "#006411"},
                                   {7, "Brown", "#562c05"}};
    return list;
}
QString name(int label)
{
    return label >= 1 && label <= 7 ? labels()[label - 1].name : QString();
}
QString color(int label)
{
    return label >= 1 && label <= 7 ? labels()[label - 1].color : QString();
}
int label(const QString &path, const QString &profile)
{
    if (path.isEmpty())
        return 0;
    const auto native = QFile::encodeName(path);
    char value[4]{};
    const auto size = lgetxattr(native.constData(), attribute, value, sizeof value - 1);
    if (size == 1 && value[0] >= '1' && value[0] <= '7')
        return value[0] - '0';
    if (size >= 0)
        return 0; // An attribute that is not a label.
    // No attribute, or none allowed here: the profile's record, if any.
    if (const auto error = errno; error != ENODATA && !unsupported(error))
        return 0;
    const auto stored = readDatabase(profile).value(QFileInfo(path).absoluteFilePath()).toInt();
    return stored >= 1 && stored <= 7 ? stored : 0;
}
bool setLabel(const QString &path, int label, const QString &profile)
{
    if (path.isEmpty() || label < 0 || label > 7 || !QFileInfo(path).exists())
        return false;
    const auto native = QFile::encodeName(path);
    const auto absolute = QFileInfo(path).absoluteFilePath();
    int result;
    if (label == 0)
        result = lremovexattr(native.constData(), attribute) == 0 || errno == ENODATA ? 0 : -1;
    else
    {
        const char value = char('0' + label);
        result = lsetxattr(native.constData(), attribute, &value, 1, 0);
    }
    // Before reading the profile, which sets errno again.
    const auto error = errno;
    auto database = readDatabase(profile);
    if (result == 0)
    {
        // The attribute is the record; forget an older profile entry for it.
        if (database.contains(absolute))
        {
            database.remove(absolute);
            writeDatabase(profile, database);
        }
        return true;
    }
    if (!unsupported(error))
        return false;
    if (label == 0)
        database.remove(absolute);
    else
        database.insert(absolute, label);
    return writeDatabase(profile, database);
}
} // namespace FileLabels
