#pragma once
#include <QObject>
#include <QVariantMap>

// The Memory control panel's facts (Lily Hewitt's makeover; 9.2.2 captured 2026-11-12). 9.2.2's
// three settings have Linux counterparts that the kernel and the system manage, so the panel shows
// them as they are and does not change them: Disk Cache is the page cache, Virtual Memory is swap
// (zram on Arch installs), and the RAM Disk is /tmp when it is a tmpfs.
class MemoryInfo final : public QObject
{
    Q_OBJECT
    // {builtIn, cache, swapOn, swapSize, swapUsed, swapKind, ramDisk, ramDiskSize, ramDiskUsed}: bytes.
    Q_PROPERTY(QVariantMap facts READ facts NOTIFY changed)
  public:
    // root: where proc/meminfo, proc/swaps and proc/mounts are read ("/" on the computer; tests
    // name a folder); tmp: the folder whose file system is the RAM disk.
    MemoryInfo(QString root, QString tmp, QObject *parent = nullptr);
    QVariantMap facts() const { return m_facts; }
    Q_INVOKABLE void refresh();
  signals:
    void changed();

  private:
    QString m_root, m_tmp;
    QVariantMap m_facts;
};
