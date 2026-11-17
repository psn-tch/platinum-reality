#include "file_search.hpp"

#include <QDateTime>
#include <algorithm>
#include <QDir>
#include <QDirIterator>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QMimeDatabase>
#include <QPointer>
#include <QStorageInfo>
#include <QtConcurrent/QtConcurrentRun>

FileSearch::FileSearch(QString home, QString diskName, QStringList drives, QObject *parent, QString mounts)
    : QObject(parent), m_home(QDir(home).absolutePath()), m_diskName(std::move(diskName)), m_mounts(std::move(mounts)),
      m_drives(std::move(drives))
{
    refreshVolumes();
}

namespace
{
// mountinfo escapes a space, tab, newline and backslash as \ooo.
QString unescaped(const QByteArray &field)
{
    QByteArray result;
    for (qsizetype i = 0; i < field.size(); ++i)
        if (field.at(i) == '\\' && i + 3 < field.size() && field.mid(i + 1, 3).toInt(nullptr, 8) > 0)
        {
            result.append(char(field.mid(i + 1, 3).toInt(nullptr, 8)));
            i += 3;
        }
        else
            result.append(field.at(i));
    return QFile::decodeName(result);
}
bool within(const QString &folder, const QString &path) { return path == folder || path.startsWith(folder + "/"); }
} // namespace

QStringList FileSearch::dataDisks() const
{
    static const QStringList kinds{"ext2", "ext3", "ext4", "btrfs", "xfs", "f2fs", "jfs", "reiserfs", "bcachefs",
                                   "ntfs", "ntfs3", "fuseblk", "exfat", "vfat", "hfsplus"};
    static const QStringList system{"/boot", "/efi", "/usr", "/var", "/opt", "/etc", "/root", "/nix", "/gnu",
                                    "/snap", "/proc", "/sys", "/dev", "/run", "/tmp"};
    QFile file(m_mounts);
    if (m_mounts.isEmpty() || !file.open(QIODevice::ReadOnly))
        return {};
    // "36 35 98:0 / /mnt/movies rw,noatime master:1 - ntfs3 /dev/sda2 rw,uid=1000": the mount point
    // is the fifth field, the type and source follow the "-".
    struct Mount
    {
        QString point, kind, source;
    };
    QList<Mount> mounts;
    QString rootSource;
    for (const auto &line : file.readAll().split('\n'))
    {
        const auto fields = line.split(' ');
        const auto dash = fields.indexOf("-");
        if (fields.size() < 5 || dash < 5 || dash + 2 >= fields.size())
            continue;
        const Mount mount{unescaped(fields.at(4)), QString::fromUtf8(fields.at(dash + 1)), unescaped(fields.at(dash + 2))};
        if (mount.point == "/")
            rootSource = mount.source;
        mounts.append(mount);
    }
    QStringList result, sources;
    // Shortest mount point first, so a disk mounted twice is offered where it is mounted highest.
    std::stable_sort(mounts.begin(), mounts.end(), [](const Mount &a, const Mount &b) { return a.point.size() < b.point.size(); });
    for (const auto &mount : std::as_const(mounts))
    {
        if (!kinds.contains(mount.kind) || !mount.source.startsWith("/dev/") || mount.source.startsWith("/dev/loop") ||
            mount.source == rootSource || mount.point == "/" || sources.contains(mount.source) ||
            within(mount.point, m_home) || within(m_home, mount.point) ||
            std::any_of(system.cbegin(), system.cend(), [&](const QString &folder) { return within(folder, mount.point); }) ||
            std::any_of(m_drives.cbegin(), m_drives.cend(), [&](const QString &folder) { return within(QDir(folder).absolutePath(), mount.point); }) ||
            !QFileInfo(mount.point).isDir() || !QFileInfo(mount.point).isReadable())
            continue;
        sources << mount.source;
        result << mount.point;
    }
    return result;
}

FileSearch::~FileSearch() { cancel(); }

void FileSearch::refreshVolumes()
{
    QHash<QString, bool> on;
    for (const auto &volume : std::as_const(m_volumes))
        on.insert(volume.toMap().value("path").toString(), volume.toMap().value("on").toBool());
    QVariantList volumes{QVariantMap{{"name", m_diskName}, {"path", m_home}, {"on", on.value(m_home, true)}}};
    for (const auto &folder : std::as_const(m_drives))
        for (const auto &drive : QDir(folder).entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name))
            if (drive.isReadable() && volumes.size() < 32)
                volumes.append(QVariantMap{{"name", drive.fileName()}, {"path", drive.absoluteFilePath()},
                                           {"on", on.value(drive.absoluteFilePath(), false)}});
    // Named by the disk's label (Potato's "Movies"), else its folder.
    for (const auto &disk : dataDisks())
        if (volumes.size() < 32)
        {
            const QStorageInfo storage(disk);
            const auto name = storage.rootPath() == disk && !storage.name().isEmpty() ? storage.name() : QDir(disk).dirName();
            volumes.append(QVariantMap{{"name", name}, {"path", disk}, {"on", on.value(disk, false)}});
        }
    if (volumes != m_volumes)
    {
        m_volumes = volumes;
        emit volumesChanged();
    }
}

void FileSearch::setVolumeOn(const QString &path, bool on)
{
    for (auto &volume : m_volumes)
    {
        auto map = volume.toMap();
        if (map.value("path").toString() == path && map.value("on").toBool() != on)
        {
            map.insert("on", on);
            volume = map;
            emit volumesChanged();
        }
    }
}

void FileSearch::cancel()
{
    if (m_cancel)
        m_cancel->store(true);
    m_cancel.reset();
    ++m_generation;
    if (m_searching)
    {
        m_searching = false;
        emit searchingChanged();
    }
}

void FileSearch::finish(quint64 generation)
{
    if (generation != m_generation)
        return;
    m_cancel.reset();
    m_searching = false;
    emit searchingChanged();
}

void FileSearch::search(const QString &text, bool contents)
{
    cancel();
    m_results.clear();
    m_truncated = false;
    emit resultsChanged();
    const auto wanted = text.trimmed();
    if (wanted.isEmpty())
        return;
    QStringList roots;
    for (const auto &volume : std::as_const(m_volumes))
        if (volume.toMap().value("on").toBool())
            roots << volume.toMap().value("path").toString();
    if (roots.isEmpty())
        return;
    const auto generation = ++m_generation;
    auto cancelled = std::make_shared<std::atomic_bool>(false);
    m_cancel = cancelled;
    m_searching = true;
    emit searchingChanged();
    QPointer<FileSearch> self(this);
    // Results reach the window in batches, at most every 150 ms, and only for this search.
    const auto deliver = [self, generation](QVariantList batch, bool more, bool done)
    {
        QMetaObject::invokeMethod(
            self.data(), [self, generation, batch = std::move(batch), more, done]
            {
                if (!self || generation != self->m_generation)
                    return;
                self->m_results.append(batch);
                self->m_truncated = self->m_truncated || more;
                if (!batch.isEmpty() || more)
                    emit self->resultsChanged();
                if (done)
                    self->finish(generation);
            },
            Qt::QueuedConnection);
    };
    (void)QtConcurrent::run([roots, wanted, contents, cancelled, deliver]
    {
        QMimeDatabase mimes;
        QVariantList batch;
        QElapsedTimer sent;
        sent.start();
        int found = 0;
        bool more = false;
        const auto matches = [&](const QFileInfo &info)
        {
            if (!contents)
                return info.fileName().contains(wanted, Qt::CaseInsensitive);
            if (!info.isFile() || info.size() > maximumContentsBytes)
                return false;
            QFile file(info.absoluteFilePath());
            if (!file.open(QIODevice::ReadOnly))
                return false;
            const auto data = file.readAll();
            if (data.left(1024).contains('\0'))
                return false; // Not text.
            return QString::fromUtf8(data).contains(wanted, Qt::CaseInsensitive);
        };
        for (const auto &root : roots)
        {
            QDirIterator walk(root, QDir::AllEntries | QDir::NoDotAndDotDot | QDir::NoSymLinks,
                              QDirIterator::Subdirectories);
            while (walk.hasNext() && !cancelled->load())
            {
                const auto info = walk.nextFileInfo();
                if (!matches(info))
                    continue;
                if (found >= maximumResults)
                {
                    more = true;
                    break;
                }
                ++found;
                const bool folder = info.isDir();
                batch.append(QVariantMap{
                    {"name", info.fileName()},
                    {"path", info.absoluteFilePath()},
                    {"folder", folder},
                    {"kind", folder ? QString("folder") : mimes.mimeTypeForFile(info, QMimeDatabase::MatchExtension).comment()},
                    {"modified", info.lastModified().toMSecsSinceEpoch()},
                    {"size", folder ? qint64(-1) : info.size()}});
                if (sent.elapsed() > 150)
                {
                    deliver(std::exchange(batch, {}), false, false);
                    sent.restart();
                }
            }
            if (more || cancelled->load())
                break;
        }
        deliver(batch, more, true);
    });
}

QVariantList FileSearch::location(const QString &path) const
{
    // The deepest checked-or-not volume the item is on, then each folder below it.
    QString root;
    QString disk;
    for (const auto &volume : m_volumes)
    {
        const auto candidate = volume.toMap().value("path").toString();
        if ((path == candidate || path.startsWith(candidate + "/")) && candidate.size() > root.size())
        {
            root = candidate;
            disk = volume.toMap().value("name").toString();
        }
    }
    if (root.isEmpty())
        return {};
    QVariantList parts{QVariantMap{{"name", disk}, {"kind", "disk"}, {"depth", 0}}};
    const auto rest = path.mid(root.size() + 1).split('/', Qt::SkipEmptyParts);
    for (qsizetype i = 0; i < rest.size(); ++i)
    {
        const bool last = i == rest.size() - 1;
        parts.append(QVariantMap{{"name", rest.at(i)},
                                 {"kind", last && !QFileInfo(path).isDir() ? "file" : "folder"},
                                 {"depth", int(i + 1)}});
    }
    return parts;
}
