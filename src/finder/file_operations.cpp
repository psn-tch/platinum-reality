#include "file_operations.hpp"
#include <algorithm>
#include <QDirIterator>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QSaveFile>
#include <QSettings>
#include <QUrl>
#include <QUuid>
#include <gio/gio.h>
#include <cerrno>
#include <cstring>
#include <dirent.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

namespace
{
bool exists(const QString &p)
{
    QFileInfo f(p);
    return f.exists() || f.isSymLink();
}
bool within(const QString &base, const QString &path)
{
    return !base.isEmpty() && (path == base || path.startsWith(base + "/"));
}
bool validName(const QString &name)
{
    return !name.isEmpty() && name != "." && name != ".." && !name.contains('/') &&
           !name.contains(QChar::Null);
}
FileResult failure(QString e)
{
    return {false, {}, std::move(e)};
}
QString errorText(GError *error)
{
    QString s = error ? QString::fromUtf8(error->message) : QStringLiteral("File operation failed");
    if (error)
        g_error_free(error);
    return s;
}
GFile *gfile(const QString &p)
{
    return g_file_new_for_path(QFile::encodeName(p).constData());
}
using FileIdentity = QPair<quint64, quint64>;
FileIdentity identity(const struct stat &entry)
{
    return {quint64(entry.st_dev), quint64(entry.st_ino)};
}
struct CopiedSources
{
    QHash<QString, struct stat> entries;
    // Unlinking one of our copied hard links changes the same inode's ctime and link count.
    QHash<FileIdentity, struct stat> unlinked;
};
bool sameIdentity(const struct stat &a, const struct stat &b)
{
    return a.st_dev == b.st_dev && a.st_ino == b.st_ino && a.st_mode == b.st_mode;
}
bool sameVersion(const struct stat &a, const struct stat &b)
{
    return sameIdentity(a, b) && a.st_size == b.st_size && a.st_nlink == b.st_nlink &&
           a.st_mtim.tv_sec == b.st_mtim.tv_sec && a.st_mtim.tv_nsec == b.st_mtim.tv_nsec &&
           a.st_ctim.tv_sec == b.st_ctim.tv_sec && a.st_ctim.tv_nsec == b.st_ctim.tv_nsec;
}
QString changedSource()
{
    return "The source changed while it was being copied. Its changed items were left in place.";
}
bool unchangedSources(const CopiedSources &copied, QString &error)
{
    for (auto it = copied.entries.cbegin(); it != copied.entries.cend(); ++it)
    {
        struct stat current {};
        if (lstat(QFile::encodeName(it.key()).constData(), &current) != 0 || !sameVersion(current, it.value()))
        {
            error = changedSource();
            return false;
        }
    }
    return true;
}
FileResult moveEntry(const QString &source, const QString &target)
{
    if (exists(target))
        return failure("An item with that name already exists.");
    GFile *from = gfile(source);
    GFile *to = gfile(target);
    GError *error = nullptr;
    bool ok =
        g_file_move(from, to, GFileCopyFlags(G_FILE_COPY_NOFOLLOW_SYMLINKS | G_FILE_COPY_NO_FALLBACK_FOR_MOVE),
                    nullptr, nullptr, nullptr, &error);
    g_object_unref(from);
    g_object_unref(to);
    return ok ? FileResult{true, target, {}} : failure(errorText(error));
}
bool copyTree(const QString &source, const QString &target, GCancellable *cancel,
              const std::shared_ptr<std::atomic_bool> &stop, QString &error,
              const std::shared_ptr<CopyProgress> &bytes = {}, CopiedSources *copied = nullptr)
{
    if (stop && stop->load())
        g_cancellable_cancel(cancel);
    if (g_cancellable_is_cancelled(cancel))
    {
        error = "Copy canceled.";
        return false;
    }
    if (copied)
    {
        struct stat original {};
        if (lstat(QFile::encodeName(source).constData(), &original) != 0)
        {
            error = changedSource();
            return false;
        }
        copied->entries.insert(source, original);
    }
    QFileInfo info(source);
    if (info.isDir() && !info.isSymLink())
    {
        if (!QDir().mkdir(target))
        {
            error = "Could not create destination folder.";
            return false;
        }
        // Enumeration errors must fail the copy, rather than publishing an
        // apparently successful empty folder when access was denied.
        GFile *directory = gfile(source);
        GError *enumerationError = nullptr;
        auto *entries = g_file_enumerate_children(directory, G_FILE_ATTRIBUTE_STANDARD_NAME,
                                                  G_FILE_QUERY_INFO_NOFOLLOW_SYMLINKS, cancel,
                                                  &enumerationError);
        g_object_unref(directory);
        if (!entries)
        {
            error = errorText(enumerationError);
            return false;
        }
        bool complete = true;
        while (auto *entry = g_file_enumerator_next_file(entries, cancel, &enumerationError))
        {
            const auto name = QFile::decodeName(g_file_info_get_name(entry));
            g_object_unref(entry);
            if (!copyTree(QDir(source).filePath(name), QDir(target).filePath(name), cancel, stop,
                          error, bytes, copied))
            {
                complete = false;
                break;
            }
        }
        if (enumerationError)
        {
            error = errorText(enumerationError);
            complete = false;
        }
        g_object_unref(entries);
        if (!complete)
            return false;
        GFile *from = gfile(source);
        GFile *to = gfile(target);
        GError *e = nullptr;
        // Do not ask a directory backend to set read-only standard::size.
        // Directory ACL/xattr fidelity is not yet qualified; preserve the
        // explicit portable timestamp/mode subset instead of claiming all.
        GFileInfo *metadata = g_file_query_info(
            from, "unix::mode,time::modified,time::modified-usec,time::access,time::access-usec",
            G_FILE_QUERY_INFO_NOFOLLOW_SYMLINKS, cancel, &e);
        bool ok = metadata && g_file_set_attributes_from_info(
                                  to, metadata, G_FILE_QUERY_INFO_NOFOLLOW_SYMLINKS, cancel, &e);
        // FAT and exFAT flash drives can't store Unix modes: the folder is copied without them, as
        // single files already are (g_file_copy treats metadata it can't set as non-fatal).
        if (!ok && metadata && e && g_error_matches(e, G_IO_ERROR, G_IO_ERROR_NOT_SUPPORTED))
        {
            g_clear_error(&e);
            ok = true;
        }
        if (metadata)
            g_object_unref(metadata);
        g_object_unref(from);
        g_object_unref(to);
        if (!ok)
        {
            error = errorText(e);
            return false;
        }
        return true;
    }
    GFile *from = gfile(source);
    GFile *to = gfile(target);
    GError *e = nullptr;
    struct Progress
    {
        GCancellable *cancel;
        const std::shared_ptr<std::atomic_bool> *stop;
        CopyProgress *bytes;
        qint64 base;
    } progress{cancel, &stop, bytes.get(), bytes ? bytes->done.load() : 0};
    auto callback = [](goffset current, goffset, gpointer data)
    {
        auto *p = static_cast<Progress *>(data);
        if (p->bytes)
            p->bytes->done.store(p->base + current);
        if (*p->stop && (*p->stop)->load())
            g_cancellable_cancel(p->cancel);
    };
    bool ok = g_file_copy(from, to,
                          GFileCopyFlags(G_FILE_COPY_NOFOLLOW_SYMLINKS | G_FILE_COPY_ALL_METADATA),
                          cancel, callback, &progress, &e);
    g_object_unref(from);
    g_object_unref(to);
    if (!ok)
        error = errorText(e);
    return ok;
}
QString uniqueName(const QString &directory, const QString &name)
{
    QString result = QDir(directory).filePath(name);
    int suffix = 1;
    while (exists(result))
        result = QDir(directory).filePath(name + " " + QString::number(suffix++));
    return result;
}
void removeCreatedStage(const QString &p)
{
    QFileInfo f(p);
    if (f.isDir() && !f.isSymLink())
        QDir(p).removeRecursively();
    else
        QFile::remove(p);
}
bool removeBelow(int parent, const char *name, dev_t device, QString &error,
                 CopiedSources *copied = nullptr, const QString &source = {});
// Moves an item to Trash, back, or to another folder. Across disks, copy beside the target,
// publish, and then remove the unchanged sources. GIO's copy/delete fallback cannot detect edits
// made during copying, so it is disabled even for individual files. If removal fails, the copy stays (ok
// is false, path is the copy). Only entries whose copied identities and versions still match
// may be removed. This detects source changes; it does not provide an atomic filesystem snapshot.
FileResult moveAcrossDisks(const QString &source, const QString &target)
{
    const QFileInfo item(source);
    if (FileOperations::sameVolume({source}, QFileInfo(target).absolutePath()))
        return moveEntry(source, target);
    if (exists(target))
        return failure("An item with that name already exists.");
    const auto stage = QFileInfo(target).absolutePath() + "/.frankenstein-move-" + QUuid::createUuid().toString(QUuid::Id128);
    GCancellable *token = g_cancellable_new();
    QString error;
    CopiedSources copied;
    const bool complete = copyTree(source, stage, token, {}, error, {}, &copied);
    g_object_unref(token);
    if (!complete || !unchangedSources(copied, error))
    {
        removeCreatedStage(stage);
        return failure(error);
    }
    auto result = moveEntry(stage, target);
    if (!result.ok)
    {
        removeCreatedStage(stage);
        return result;
    }
    const int parent = open(QFile::encodeName(item.absolutePath()).constData(), O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    struct stat original {};
    const bool removed = parent >= 0 &&
                         fstatat(parent, QFile::encodeName(item.fileName()).constData(), &original, AT_SYMLINK_NOFOLLOW) == 0 &&
                         removeBelow(parent, QFile::encodeName(item.fileName()).constData(), original.st_dev,
                                     error, &copied, source);
    if (parent >= 0)
        close(parent);
    if (!removed)
        return {false, target, "“" + item.fileName() + "” was copied, but not all of it could be removed from its disk" +
                                   (error.isEmpty() ? QString(".") : ": " + error)};
    return result;
}
} // namespace
FileOperations::FileOperations(QString root, QString trashBase, QStringList extraRoots)
    : m_root(QFileInfo(root).canonicalFilePath()),
      m_trash(QDir::cleanPath(QFileInfo(trashBase).absoluteFilePath()))
{
    m_roots << m_root;
    // The mounted-drives folder (/run/media/<user>) only exists once the first drive is mounted,
    // usually after the desktop started: until then, its plain absolute path stands for it.
    for (const auto &extra : extraRoots)
    {
        auto canonical = QFileInfo(extra).canonicalFilePath();
        if (canonical.isEmpty() && QFileInfo(extra).isAbsolute() && !QFileInfo(extra).exists())
            canonical = QDir::cleanPath(extra);
        if (!canonical.isEmpty() && !m_roots.contains(canonical))
            m_roots << canonical;
    }
}
bool FileOperations::inside(const QString &canonicalPath) const
{
    return std::any_of(m_roots.begin(), m_roots.end(),
                       [&](const QString &root) { return within(root, canonicalPath); });
}
bool FileOperations::readableDirectory(const QString &path) const
{
    const auto canonical = QFileInfo(path).canonicalFilePath();
    const auto trashFiles = QFileInfo(m_trash + "/files").canonicalFilePath();
    return QFileInfo(path).isDir() &&
           (inside(canonical) || (!trashFiles.isEmpty() && within(trashFiles, canonical)) ||
            (FinderPlaces::wholeComputer && !canonical.isEmpty() && QFileInfo(canonical).isReadable() &&
             QFileInfo(canonical).isExecutable()));
}
bool FileOperations::readableFile(const QString &path) const
{
    const auto canonical = QFileInfo(path).canonicalFilePath();
    const auto trashFiles = QFileInfo(m_trash + "/files").canonicalFilePath();
    return QFileInfo(path).isFile() &&
           (inside(canonical) || (!trashFiles.isEmpty() && within(trashFiles, canonical)) ||
            (FinderPlaces::wholeComputer && !canonical.isEmpty() && QFileInfo(canonical).isReadable()));
}
bool FileOperations::writableEntry(const QString &path) const
{
    const auto absolute = QDir::cleanPath(QFileInfo(path).absoluteFilePath());
    if (m_roots.contains(absolute))
        return false;
    const auto parent = QFileInfo(absolute).dir().canonicalPath();
    return std::any_of(m_roots.begin(), m_roots.end(), [&](const QString &root)
                       { return within(root, absolute) && within(root, parent); });
}
FileResult FileOperations::copy(const QString &source, const QString &directory,
                                std::shared_ptr<std::atomic_bool> cancel,
                                std::shared_ptr<CopyProgress> progress) const
{
    if (!writableEntry(source) || !inside(QFileInfo(directory).canonicalFilePath()))
        return failure("The operation is outside the selected workspace.");
    const QString target = QDir(directory).filePath(QFileInfo(source).fileName());
    if (!exists(source))
        return failure("The source no longer exists.");
    if (exists(target))
        return failure("An item with that name already exists.");
    if (QFileInfo(source).isDir() && !QFileInfo(source).isSymLink() &&
        within(QFileInfo(source).canonicalFilePath(), QFileInfo(directory).canonicalFilePath()))
        return failure("A folder cannot be copied into itself.");
    const QString stage = QDir(directory).filePath(".frankenstein-copy-" +
                                                   QUuid::createUuid().toString(QUuid::Id128));
    GCancellable *token = g_cancellable_new();
    QString error;
    bool ok = copyTree(source, stage, token, cancel, error, progress);
    g_object_unref(token);
    if (!ok)
    {
        removeCreatedStage(stage);
        return failure(error);
    }
    auto result = moveEntry(stage, target);
    if (!result.ok)
        removeCreatedStage(stage);
    return result;
}
bool FileOperations::sameVolume(const QStringList &sources, const QString &directory)
{
    struct stat folder{};
    if (::stat(QFile::encodeName(directory).constData(), &folder) != 0)
        return false;
    return std::all_of(sources.cbegin(), sources.cend(), [&](const QString &source)
    {
        struct stat item{};
        return ::lstat(QFile::encodeName(source).constData(), &item) == 0 && item.st_dev == folder.st_dev;
    });
}
FileResult FileOperations::move(const QString &source, const QString &directory) const
{
    if (!writableEntry(source) || !inside(QFileInfo(directory).canonicalFilePath()))
        return failure("The operation is outside the selected workspace.");
    if (QFileInfo(source).isDir() && !QFileInfo(source).isSymLink() &&
        within(QFileInfo(source).canonicalFilePath(), QFileInfo(directory).canonicalFilePath()))
        return failure("A folder cannot be moved into itself.");
    return moveAcrossDisks(source, QDir(directory).filePath(QFileInfo(source).fileName()));
}
FileResult FileOperations::duplicate(const QString &source) const
{
    if (!writableEntry(source) || !exists(source))
        return failure("The item is unavailable.");
    const auto parent = QFileInfo(source).absolutePath();
    const QFileInfo info(source);
    const auto suffix = !info.isDir() && info.fileName().lastIndexOf('.') > 0 && !info.suffix().isEmpty()
                            ? "." + info.suffix() : QString();
    const auto stem = suffix.isEmpty() ? info.fileName() : info.completeBaseName();
    // Keep the Linux association suffix usable. This naming policy is a native
    // adaptation, not a claim about classic Finder's type/creator-based naming.
    const auto base = stem + " copy";
    auto target = QDir(parent).filePath(base + suffix);
    for (int number = 2; exists(target); ++number)
        target = QDir(parent).filePath(base + " " + QString::number(number) + suffix);
    const auto stageDir = QDir(parent).filePath(".frankenstein-duplicate-" +
                                                QUuid::createUuid().toString(QUuid::Id128));
    if (!QDir().mkdir(stageDir))
        return failure("Could not prepare duplicate.");
    auto result = copy(source, stageDir);
    if (result.ok)
        result = moveEntry(result.path, target);
    removeCreatedStage(stageDir);
    return result;
}
FileResult FileOperations::rename(const QString &source, const QString &name) const
{
    if (!writableEntry(source) || !validName(name))
        return failure("The name or item is invalid.");
    if (name == QFileInfo(source).fileName())
        return {true, source, {}};
    return moveEntry(source, QFileInfo(source).dir().filePath(name));
}
FileResult FileOperations::createFolder(const QString &directory, const QString &name) const
{
    if (!inside(QFileInfo(directory).canonicalFilePath()) || !validName(name))
        return failure("The folder name or location is invalid.");
    const QString path = QDir(directory).filePath(name);
    if (exists(path))
        return failure("An item with that name already exists.");
    return QDir().mkdir(path) ? FileResult{true, path, {}}
                              : failure("Could not create the folder.");
}
FileResult FileOperations::createFile(const QString &directory, const QString &name) const
{
    if (!inside(QFileInfo(directory).canonicalFilePath()) || !validName(name))
        return failure("The file name or location is invalid.");
    const QFileInfo info(name);
    const auto suffix = name.lastIndexOf('.') > 0 && !info.suffix().isEmpty() ? "." + info.suffix() : QString();
    const auto stem = suffix.isEmpty() ? name : info.completeBaseName();
    auto path = QDir(directory).filePath(name);
    for (int number = 2; exists(path); ++number)
        path = QDir(directory).filePath(stem + " " + QString::number(number) + suffix);
    QFile file(path);
    if (!file.open(QIODevice::NewOnly | QIODevice::WriteOnly))
        return failure("Could not create the file.");
    return {true, path, {}};
}
FileResult FileOperations::makeAlias(const QString &source) const
{
    if (!writableEntry(source) || !exists(source))
        return failure("The item is unavailable.");
    const QFileInfo info(source);
    const auto base = info.fileName() + " alias";
    auto target = QDir(info.absolutePath()).filePath(base);
    for (int number = 2; exists(target); ++number)
        target = QDir(info.absolutePath()).filePath(base + " " + QString::number(number));
    return QFile::link(info.absoluteFilePath(), target) ? FileResult{true, target, {}}
                                                       : failure("Could not make the alias.");
}
FileResult FileOperations::trash(const QString &source) const
{
    if (!writableEntry(source) || !exists(source))
        return failure("The item cannot be moved to Trash.");
    const auto absolute = QDir::cleanPath(QFileInfo(source).absoluteFilePath());
    if (within(absolute, m_trash) || within(m_trash, absolute))
        return failure("Trash and its containing folders cannot be moved into Trash.");
    QDir().mkpath(m_trash + "/files");
    QDir().mkpath(m_trash + "/info");
    QString target = uniqueName(m_trash + "/files", QFileInfo(source).fileName());
    while (exists(m_trash + "/info/" + QFileInfo(target).fileName() + ".trashinfo"))
        target += " " + QUuid::createUuid().toString(QUuid::Id128).left(8);
    const QString info = m_trash + "/info/" + QFileInfo(target).fileName() + ".trashinfo";
    QSaveFile metadata(info);
    if (!metadata.open(QIODevice::WriteOnly))
        return failure(metadata.errorString());
    metadata.write(
        "[Trash Info]\nPath=" + QUrl::toPercentEncoding(QFileInfo(source).absoluteFilePath(), "/") +
        "\nDeletionDate=" + QDateTime::currentDateTime().toString("yyyy-MM-ddTHH:mm:ss").toUtf8() +
        "\n");
    if (!metadata.commit())
        return failure(metadata.errorString());
    auto result = moveAcrossDisks(source, target);
    if (!result.ok && !exists(target))
        QFile::remove(info);
    return result;
}
FileResult FileOperations::restore(const QString &trashedPath) const
{
    if (QFileInfo(trashedPath).absolutePath() != m_trash + "/files")
        return failure("Select an item at the top level of Trash.");
    const QString info = m_trash + "/info/" + QFileInfo(trashedPath).fileName() + ".trashinfo";
    QFile metadata(info);
    if (!metadata.open(QIODevice::ReadOnly))
        return failure("The original location is unavailable.");
    QString target;
    while (!metadata.atEnd())
    {
        auto line = metadata.readLine().trimmed();
        if (line.startsWith("Path="))
            target = QUrl::fromPercentEncoding(line.mid(5));
    }
    if (!writableEntry(target) || !QFileInfo(QFileInfo(target).absolutePath()).isDir())
        return failure("The original folder is unavailable or outside the workspace.");
    auto result = moveAcrossDisks(trashedPath, target);
    if (result.ok)
        QFile::remove(info);
    return result;
}

namespace
{
// Removes `name` inside the open directory `parent` without following links or
// crossing onto another device. Returns false after recording the first error.
bool removeBelow(int parent, const char *name, dev_t device, QString &error,
                 CopiedSources *copied, const QString &source)
{
    struct stat entry {};
    if (fstatat(parent, name, &entry, AT_SYMLINK_NOFOLLOW) != 0)
    {
        if (errno == ENOENT && !copied)
            return true;
        error = QString::fromLocal8Bit(std::strerror(errno));
        return false;
    }
    if (copied)
    {
        const auto expected = copied->entries.constFind(source);
        if (expected == copied->entries.cend())
        {
            error = changedSource();
            return false;
        }
        auto version = expected.value();
        const auto unlinked = copied->unlinked.constFind(identity(version));
        if (unlinked != copied->unlinked.cend())
        {
            version.st_ctim = unlinked->st_ctim;
            version.st_nlink = unlinked->st_nlink;
        }
        if (!sameVersion(entry, version))
        {
            error = changedSource();
            return false;
        }
    }
    if (entry.st_dev != device)
    {
        error = "An item in Trash is on another disk and was left in place.";
        return false;
    }
    if (S_ISDIR(entry.st_mode))
    {
        const int directory = openat(parent, name, O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
        if (directory < 0)
        {
            error = QString::fromLocal8Bit(std::strerror(errno));
            return false;
        }
        struct stat opened {};
        if (copied && (fstat(directory, &opened) != 0 || !sameVersion(entry, opened)))
        {
            close(directory);
            error = changedSource();
            return false;
        }
        DIR *stream = fdopendir(directory);
        if (!stream)
        {
            close(directory);
            error = QString::fromLocal8Bit(std::strerror(errno));
            return false;
        }
        bool ok = true;
        QList<QByteArray> children;
        while (true)
        {
            errno = 0;
            auto *child = readdir(stream);
            if (!child)
            {
                if (errno)
                {
                    error = QString::fromLocal8Bit(std::strerror(errno));
                    ok = false;
                }
                break;
            }
            if (std::strcmp(child->d_name, ".") != 0 && std::strcmp(child->d_name, "..") != 0)
            {
                if (copied && !copied->entries.contains(QDir(source).filePath(QFile::decodeName(child->d_name))))
                {
                    error = changedSource();
                    ok = false;
                    break;
                }
                children.push_back(child->d_name);
            }
        }
        for (const auto &child : children)
            if (!ok || !removeBelow(dirfd(stream), child.constData(), device, error, copied,
                                    copied ? QDir(source).filePath(QFile::decodeName(child)) : QString()))
            {
                ok = false;
                break;
            }
        closedir(stream);
        if (!ok)
            return false;
        // Deleting children changes directory timestamps, so recheck identity rather than
        // its old version. A newly added child makes AT_REMOVEDIR fail and stays untouched.
        struct stat current {};
        if (copied && (fstatat(parent, name, &current, AT_SYMLINK_NOFOLLOW) != 0 || !sameIdentity(entry, current)))
        {
            error = changedSource();
            return false;
        }
    }
    // Hold a hard-linked inode so we can observe only our own unlink's metadata update.
    // O_PATH does not read it, follow a symlink, or require read access to its contents.
    const bool hardLink = copied && !S_ISDIR(entry.st_mode) && entry.st_nlink > 1;
    const int held = hardLink ? openat(parent, name, O_PATH | O_NOFOLLOW | O_CLOEXEC) : -1;
    struct stat before {};
    if (hardLink && (held < 0 || fstat(held, &before) != 0 || !sameVersion(entry, before)))
    {
        if (held >= 0)
            close(held);
        error = changedSource();
        return false;
    }
    if (unlinkat(parent, name, S_ISDIR(entry.st_mode) ? AT_REMOVEDIR : 0) != 0)
    {
        error = QString::fromLocal8Bit(std::strerror(errno));
        if (held >= 0)
            close(held);
        return false;
    }
    if (held >= 0)
    {
        struct stat after {};
        const bool read = fstat(held, &after) == 0;
        close(held);
        // Our unlink changes ctime and link count, but cannot change contents, mode or mtime.
        auto expected = entry;
        expected.st_ctim = after.st_ctim;
        expected.st_nlink = after.st_nlink;
        if (!read || after.st_nlink != entry.st_nlink - 1 || !sameVersion(expected, after))
        {
            error = changedSource();
            return false;
        }
        copied->unlinked.insert(identity(entry), after);
    }
    return true;
}
} // namespace

QStringList FileOperations::trashedItems() const
{
    QStringList items;
    const QFileInfo files(m_trash + "/files");
    if (!files.isDir() || files.isSymLink())
        return items;
    for (const auto &entry : QDir(files.filePath()).entryInfoList(
             QDir::AllEntries | QDir::Hidden | QDir::System | QDir::NoDotAndDotDot, QDir::Name))
        items << entry.filePath();
    return items;
}

FileResult FileOperations::removeFromTrash(const QString &trashedPath) const
{
    const QFileInfo files(m_trash + "/files");
    const QFileInfo item(trashedPath);
    if (!files.isDir() || files.isSymLink() || item.absolutePath() != files.absoluteFilePath() ||
        item.fileName().isEmpty() || item.fileName() == "." || item.fileName() == "..")
        return failure("Only items at the top level of Trash can be removed.");
    const int parent = open(QFile::encodeName(files.absoluteFilePath()).constData(),
                            O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
    if (parent < 0)
        return failure("Trash is unavailable.");
    struct stat trash {};
    QString error;
    const bool ok = fstat(parent, &trash) == 0 &&
                    removeBelow(parent, QFile::encodeName(item.fileName()).constData(), trash.st_dev,
                                error);
    close(parent);
    if (!ok)
        return failure(error.isEmpty() ? "The item could not be removed." : error);
    QFile::remove(m_trash + "/info/" + item.fileName() + ".trashinfo");
    return {true, trashedPath, {}};
}

qint64 FileOperations::measure(const QStringList &paths)
{
    qint64 bytes = 0, visited = 0;
    for (const auto &path : paths)
    {
        const QFileInfo item(path);
        if (!item.isDir() || item.isSymLink())
        {
            bytes += item.isSymLink() ? 0 : item.size();
            continue;
        }
        QDirIterator walk(path, QDir::Files | QDir::Hidden | QDir::System | QDir::NoSymLinks,
                          QDirIterator::Subdirectories);
        while (walk.hasNext() && ++visited <= 200000)
            bytes += walk.nextFileInfo().size();
    }
    return bytes;
}

QString FinderPlaces::diskName(const QString &profile)
{
    const auto name = QSettings(profile + "/finder.ini", QSettings::IniFormat).value("diskName").toString().trimmed();
    return name.isEmpty() || name.contains('/') ? QString("Hard Disk") : name.left(64);
}
