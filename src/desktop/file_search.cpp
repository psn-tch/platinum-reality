#include "file_search.hpp"

#include <QDateTime>
#include <QDir>
#include <QDirIterator>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QMimeDatabase>
#include <QPointer>
#include <QtConcurrent/QtConcurrentRun>

FileSearch::FileSearch(QString home, QString diskName, QStringList drives, QObject *parent)
    : QObject(parent), m_home(QDir(home).absolutePath()), m_diskName(std::move(diskName)), m_drives(std::move(drives))
{
    refreshVolumes();
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
