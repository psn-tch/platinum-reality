#include "memory_info.hpp"

#include <QFile>
#include <QStorageInfo>

namespace
{
QStringList lines(const QString &path)
{
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? QString::fromUtf8(file.readAll()).split('\n', Qt::SkipEmptyParts)
                                          : QStringList();
}
} // namespace

MemoryInfo::MemoryInfo(QString root, QString tmp, QObject *parent)
    : QObject(parent), m_root(std::move(root)), m_tmp(std::move(tmp))
{
    refresh();
}

void MemoryInfo::refresh()
{
    QVariantMap facts;
    // /proc/meminfo: "MemTotal:  28312632 kB".
    for (const auto &line : lines(m_root + "/proc/meminfo"))
    {
        const auto parts = line.simplified().split(' ');
        if (parts.size() < 2)
            continue;
        const auto bytes = parts.at(1).toLongLong() * 1024;
        if (parts.at(0) == "MemTotal:")
            facts.insert("builtIn", bytes);
        else if (parts.at(0) == "Cached:")
            facts.insert("cache", bytes);
    }
    // /proc/swaps: a header, then "Filename Type Size Used Priority" in KiB.
    qint64 swapSize = 0, swapUsed = 0;
    QString kind;
    const auto swaps = lines(m_root + "/proc/swaps");
    for (qsizetype i = 1; i < swaps.size(); ++i)
    {
        const auto parts = swaps.at(i).simplified().split(' ');
        if (parts.size() < 4)
            continue;
        swapSize += parts.at(2).toLongLong() * 1024;
        swapUsed += parts.at(3).toLongLong() * 1024;
        const auto name = parts.at(0).startsWith("/dev/zram") ? QString("compressed memory (zram)")
                          : parts.at(1) == "file"             ? QString("a swap file")
                                                              : QString("a swap partition");
        if (kind.isEmpty())
            kind = name;
        else if (kind != name)
            kind = "several swap areas";
    }
    facts.insert("swapOn", swapSize > 0);
    facts.insert("swapSize", swapSize);
    facts.insert("swapUsed", swapUsed);
    facts.insert("swapKind", kind);
    // The RAM disk: the tmpfs mounted at m_tmp, as /proc/mounts lists it.
    bool tmpfs = false;
    for (const auto &line : lines(m_root + "/proc/mounts"))
    {
        const auto parts = line.split(' ');
        if (parts.size() >= 3 && parts.at(1) == m_tmp)
            tmpfs = parts.at(2) == "tmpfs";
    }
    facts.insert("ramDisk", tmpfs);
    if (tmpfs)
    {
        const QStorageInfo storage(m_tmp);
        facts.insert("ramDiskSize", storage.bytesTotal());
        facts.insert("ramDiskUsed", storage.bytesTotal() - storage.bytesFree());
    }
    if (facts != m_facts)
    {
        m_facts = facts;
        emit changed();
    }
}
