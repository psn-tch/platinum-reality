#pragma once
#include <QString>
#include <QStringList>
#include <atomic>
#include <memory>

// Bytes copied so far, for a progress display (shared with the worker; atomics only).
struct CopyProgress
{
    std::atomic<qint64> done{0}, total{0};
};

struct FileResult
{
    bool ok = false;
    QString path;
    QString error;
};

// Where a Finder works, set once at startup from its command line. Tests keep the defaults: the
// profile's own Trash and no further roots. The installed session passes the user's shared Trash
// ($XDG_DATA_HOME/Trash, the same as other file managers) and the mounted-drives folder.
struct FinderPlaces
{
    static inline QString trash;
    static inline QStringList volumes;
    static QString trashFor(const QString &profile) { return trash.isEmpty() ? profile + "/data/Trash" : trash; }
    // The home folder's disk name: "Hard Disk", or the profile's finder.ini diskName (for example
    // "Macintosh HD"). Shared by the Finder and the desktop.
    static QString diskName(const QString &profile);
};

// Immutable roots; safe to use in a worker. Never silently overwrites/deletes.
// Root confinement is an application safety boundary, not a filesystem sandbox.
class FileOperations
{
  public:
    // extraRoots: further places the user may work in (mounted drives), beside the root.
    FileOperations(QString root, QString trashBase, QStringList extraRoots = {});
    QString root() const
    {
        return m_root;
    }
    QString trashBase() const
    {
        return m_trash;
    }
    bool readableDirectory(const QString &path) const;
    bool readableFile(const QString &path) const;
    FileResult copy(const QString &source, const QString &directory,
                    std::shared_ptr<std::atomic_bool> cancel = {},
                    std::shared_ptr<CopyProgress> progress = {}) const;
    // Bytes in regular files under the items (links not followed, at most 200,000 entries).
    static qint64 measure(const QStringList &paths);
    FileResult move(const QString &source, const QString &directory) const;
    FileResult duplicate(const QString &source) const;
    FileResult rename(const QString &source, const QString &name) const;
    FileResult createFolder(const QString &directory, const QString &name) const;
    FileResult trash(const QString &source) const;
    FileResult restore(const QString &trashedPath) const;
    // Special > Empty Trash…, after the user confirmed: permanently removes one
    // top-level Trash entry and its record. Links are removed, never followed;
    // nothing on another filesystem (a mount inside a trashed folder) is touched.
    FileResult removeFromTrash(const QString &trashedPath) const;
    QStringList trashedItems() const;

    // Inside the root or one of the extra roots.
    bool inside(const QString &canonicalPath) const;

  private:
    bool writableEntry(const QString &path) const;
    QString m_root, m_trash;
    QStringList m_roots;
};
