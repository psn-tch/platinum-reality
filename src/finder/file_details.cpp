#include "file_details.hpp"
#include <QDateTime>
#include <QDir>
#include <QDirIterator>
#include <QFileInfo>
#include <QLocale>
#include <QMimeDatabase>
#include <QStorageInfo>

QVariantMap fileDetails(const QString &path, const QString &kind)
{
    // Mac OS 9's Info window wording and dates ("Wed, Oct 21, 2026, 2:45 AM").
    const QLocale english(QLocale::English, QLocale::UnitedStates);
    const auto when = [&](const QDateTime &time)
    { return time.isValid() ? english.toString(time, "ddd, MMM d, yyyy, h:mm AP") : QString("Unavailable"); };
    const auto bytes = [&](qint64 size)
    {
        return english.formattedDataSize(size, 1, QLocale::DataSizeTraditionalFormat) + " (" +
               english.toString(size) + (size == 1 ? " byte)" : " bytes)");
    };
    // A shallow, bounded count: never a recursive scan, and never a folder's own inode size.
    const auto contents = [&](const QString &folder)
    {
        constexpr int limit = 10000;
        const QFileInfo info(folder);
        if (!info.isDir() || !info.isReadable() || !info.isExecutable())
            return QString("Unavailable");
        int count = 0;
        QDirIterator items(folder, QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden | QDir::System);
        while (items.hasNext() && count < limit)
        {
            items.next();
            ++count;
        }
        if (count >= limit)
            return QString("Contains %1 or more items").arg(english.toString(limit));
        return count == 1 ? QString("Contains 1 item") : QString("Contains %1 items").arg(english.toString(count));
    };
    const QFileInfo file(path);
    QVariantMap result{{"where", file.absolutePath()}, {"created", when(file.birthTime())},
                       {"modified", when(file.lastModified())}};
    if (kind == "disk")
    {
        const QStorageInfo storage(path);
        result["kindLabel"] = "Disk";
        result["capacity"] = storage.isValid() ? bytes(storage.bytesTotal()) : QString("Unavailable");
        result["available"] = storage.isValid() ? bytes(storage.bytesAvailable()) : QString("Unavailable");
    }
    else if (kind == "trash")
    {
        result["kindLabel"] = "Trash";
        result["size"] = file.isDir() ? contents(path) : QString("Empty");
    }
    else if (file.isSymLink())
    {
        // An alias names its original; it is not the original's kind or size.
        result["kindLabel"] = "Alias";
        const auto original = QDir::cleanPath(QDir(file.absolutePath()).absoluteFilePath(file.readSymLink()));
        result["original"] = QFileInfo::exists(original) ? original : original + " (missing)";
    }
    else if (file.isDir())
    {
        result["kindLabel"] = "Folder";
        result["size"] = contents(path);
    }
    else if (file.exists())
    {
        const auto type = QMimeDatabase().mimeTypeForFile(file);
        auto label = type.isDefault() ? QString("Document") : type.comment();
        if (!label.isEmpty())
            label[0] = label[0].toUpper();
        result["kindLabel"] = label;
        result["size"] = bytes(file.size());
    }
    else
    {
        result["kindLabel"] = "Unavailable";
        result["missing"] = true;
    }
    return result;
}
