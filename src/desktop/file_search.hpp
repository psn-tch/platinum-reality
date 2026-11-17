#pragma once
#include <QObject>
#include <QVariantList>
#include <atomic>
#include <memory>

// Sherlock's Files channel (Lily Hewitt, 2026-11-15; 9.2.2's Sherlock 2 captured the same day): find
// items by name, or text files by their contents, on the checked disks. The search walks the disks
// on a worker thread (no index: Linux has none we can rely on), skips hidden folders and symbolic
// links, and sends results back in batches, the first maximumResults of them. Besides the drives
// folders' drives, it offers the data disks mounted at startup (fstab), such as /mnt/movies (Lily
// Hewitt, 2026-11-16): never the system's own mounts.
class FileSearch final : public QObject
{
    Q_OBJECT
    // {name, path, on}: the home folder's disk first (on), then each mounted drive and data disk (off).
    Q_PROPERTY(QVariantList volumes READ volumes NOTIFY volumesChanged)
    // {name, path, folder, kind, modified (ms since 1970), size (bytes, -1 for a folder)}.
    Q_PROPERTY(QVariantList results READ results NOTIFY resultsChanged)
    Q_PROPERTY(bool searching READ searching NOTIFY searchingChanged)
    // More matched than maximumResults; only those are listed.
    Q_PROPERTY(bool truncated READ truncated NOTIFY resultsChanged)
  public:
    static constexpr int maximumResults = 1000;
    // Text files larger than this are not searched for contents.
    static constexpr qint64 maximumContentsBytes = 4 * 1024 * 1024;
    // home: the workspace shown as diskName; drives: folders whose subfolders are mounted drives;
    // mounts: the mount table to find data disks in (tests name a fixture; empty for none).
    FileSearch(QString home, QString diskName, QStringList drives, QObject *parent = nullptr,
               QString mounts = "/proc/self/mountinfo");
    ~FileSearch() override;
    QVariantList volumes() const { return m_volumes; }
    QVariantList results() const { return m_results; }
    bool searching() const { return m_searching; }
    bool truncated() const { return m_truncated; }
    Q_INVOKABLE void refreshVolumes();
    Q_INVOKABLE void setVolumeOn(const QString &path, bool on);
    // contents: match the text inside files instead of their names. Replaces any search going on.
    Q_INVOKABLE void search(const QString &text, bool contents);
    Q_INVOKABLE void cancel();
    // Where an item is, for the lower pane: [{name, kind ("disk", "folder" or "file"), depth}],
    // from its disk down to the item itself.
    Q_INVOKABLE QVariantList location(const QString &path) const;
    // Mount points of disks holding data, from the mount table: a filesystem on a disk (/dev/…) of a
    // kind that holds files, not the system's (/, /boot, /usr, /var…, anything sharing the root
    // filesystem's device, as btrfs subvolumes do), not the home folder or inside it, nor a drive the
    // drives folders already list. Each disk once, at its shortest mount point.
    QStringList dataDisks() const;
  signals:
    void volumesChanged();
    void resultsChanged();
    void searchingChanged();

  private:
    void finish(quint64 generation);
    QString m_home, m_diskName, m_mounts;
    QStringList m_drives;
    QVariantList m_volumes, m_results;
    bool m_searching = false, m_truncated = false;
    quint64 m_generation = 0;
    std::shared_ptr<std::atomic_bool> m_cancel;
};
