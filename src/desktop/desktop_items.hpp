#pragma once
#include "display_layout.hpp"
#include "finder/file_operations.hpp"
#include <QAbstractListModel>
#include <QFileSystemWatcher>
#include <QMap>
#include <QPoint>
#include <QRect>
#include <QSet>
#include <QSize>
#include <QFileInfo>
#include <QTimer>

// Native desktop entries. This is presentation state, not a Macintosh Desktop
// Database or an access-control boundary. File contents stay ordinary Linux files.
class DesktopItems final : public QAbstractListModel
{
    Q_OBJECT
    Q_PROPERTY(QString directory READ directory CONSTANT)
    Q_PROPERTY(int selectionCount READ selectionCount NOTIFY selectionChanged)
    Q_PROPERTY(bool busy READ busy NOTIFY operationChanged)
    Q_PROPERTY(QString operationStatus READ operationStatus NOTIFY operationChanged)
    Q_PROPERTY(bool canModifySelection READ canModifySelection NOTIFY selectionChanged)
    Q_PROPERTY(QVariantMap renameTarget READ renameTarget NOTIFY selectionChanged)
    Q_PROPERTY(QString selectedDocument READ selectedDocument NOTIFY selectionChanged)
    Q_PROPERTY(bool dragging READ dragging NOTIFY dragChanged)
    Q_PROPERTY(QString dropTarget READ dropTarget NOTIFY dragChanged)
    Q_PROPERTY(bool selecting READ selecting NOTIFY selectionAreaChanged)
    Q_PROPERTY(QRect selectionRect READ selectionRect NOTIFY selectionAreaChanged)
    // Whether Trash holds anything (the Finder can trash too); the Trash icon shows it.
    Q_PROPERTY(bool trashFull READ trashFull NOTIFY trashChanged)
  public:
    enum Role
    {
        KeyRole = Qt::UserRole + 1,
        NameRole,
        PathRole,
        KindRole,
        XRole,
        YRole,
        SelectedRole
    };
    DesktopItems(QString workspace, QString profile, QObject *parent = nullptr,
                 QSize viewport = {1120, 760});
    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;
    QString directory() const
    {
        return m_directory;
    }
    int selectionCount() const
    {
        return int(m_selected.size());
    }
    bool busy() const
    {
        return m_busy;
    }
    QString operationStatus() const
    {
        return m_operationStatus;
    }
    bool canModifySelection() const;
    QVariantMap renameTarget() const;
    QString selectedDocument() const;
    bool dragging() const
    {
        return !m_dragPositions.isEmpty();
    }
    QString dropTarget() const
    {
        return m_dropTarget;
    }
    bool selecting() const
    {
        return m_selecting;
    }
    QRect selectionRect() const
    {
        return QRect(QPoint(qMin(m_selectionOrigin.x(), m_selectionPointer.x()),
                            qMin(m_selectionOrigin.y(), m_selectionPointer.y())),
                     QPoint(qMax(m_selectionOrigin.x(), m_selectionPointer.x()),
                            qMax(m_selectionOrigin.y(), m_selectionPointer.y())));
    }
    Q_INVOKABLE void refresh();
    Q_INVOKABLE void setViewport(int width, int height);
    // Disconnect rescue affects presentation, not the stored preferred position.
    void setDisplayLayout(const DisplayLayout &layout);
    Q_INVOKABLE bool acceptsPoint(int x, int y) const;
    Q_INVOKABLE void select(QString key, bool extend = false);
    Q_INVOKABLE void clearSelection();
    Q_INVOKABLE void selectAll();
    Q_INVOKABLE bool beginSelection(int x, int y, bool extend = false);
    Q_INVOKABLE void updateSelection(int x, int y);
    Q_INVOKABLE void endSelection();
    Q_INVOKABLE void cancelSelection();
    Q_INVOKABLE void moveSelected(int dx, int dy);
    Q_INVOKABLE void beginDrag();
    Q_INVOKABLE void cancelDrag();
    Q_INVOKABLE void finishDrag(int x, int y, bool copy = false);
    Q_INVOKABLE void updateDropTarget(int x, int y);
    Q_INVOKABLE bool savePositions();
    Q_INVOKABLE void openSelected();
    Q_INVOKABLE void cleanUp();
    Q_INVOKABLE bool createFolder(QString name);
    Q_INVOKABLE bool renameItem(QString key, QString name);
    Q_INVOKABLE bool duplicateSelected();
    Q_INVOKABLE bool trashSelected();
    // Special > Empty Trash…: {items, bytes, complete} for the confirmation, then
    // permanent removal of what Trash held when the user confirmed.
    Q_INVOKABLE QVariantMap trashSummary() const;
    bool trashFull() const { return m_trashFull; }
    Q_INVOKABLE bool emptyTrash();
    Q_INVOKABLE void externalDragAt(int x, int y);
    Q_INVOKABLE void clearExternalDrag();
    bool receiveFiles(QStringList paths, QPoint point, bool copy);
    bool beginWindowDrop(bool copy);
    bool completeWindowDrop(QString directory, QString error = {});
    Q_INVOKABLE QString selectionInfo() const;
    // The single selected item's facts for a Get Info window, read once (the window stays bound to
    // that item): {key, path, name, kind, kindLabel, size, where, created, modified, original,
    // capacity, available}. Linux lacks some Mac facts; those read "Unavailable". Empty without a
    // single selection.
    Q_INVOKABLE QVariantMap selectedDetails() const;
    // Edit > Copy with icons selected, as Mac OS 9.2.2's Finder does: the selected items' names
    // as text, one per line, in reading order (top to bottom, then left to right).
    Q_INVOKABLE QString selectedNames() const;
    // Typing on the desktop selects an icon by name, as the 9.2.2 Finder does (checked in psn_tch's
    // 9.2.2 VM): the first name at or after the typed text, ignoring case, or else the last name.
    Q_INVOKABLE bool typeSelect(const QString &text);
  signals:
    void selectionChanged();
    void openRequested(QString kind, QString path);
    void errorOccurred(QString message);
    void operationChanged();
    void operationFinished(bool success, QStringList completedPaths);
    void dragChanged();
    void selectionAreaChanged();
    void trashChanged();

  private:
    struct Entry
    {
        QString key, name, path, kind, filesystemIdentity;
        bool operator==(const Entry &) const = default;
    };
    QPoint position(const Entry &entry) const;
    static bool trustedLauncher(const QFileInfo &file);
    QVector<QRect> placementAreas() const;
    QPoint fittedPosition(QPoint requested, QSize footprint = {100, 76}) const;
    void settlePlacement(const QSet<QString> &keys);
    const Entry *dropEntry(int x, int y) const;
    const Entry *externalDropEntry(QPoint point) const;
    void loadPositions();
    void arrangeMissing();
    void notifySelection();
    enum class Operation
    {
        create,
        rename,
        duplicate,
        trash,
        move,
        copy,
        erase
    };
    bool startOperation(Operation operation, QStringList paths, QString name = {},
                        Entry renamed = {}, QPoint landing = {-1, -1});
    QStringList selectedFilePaths() const;
    QVector<Entry> m_entries;
    QVector<Entry> m_rememberedEntries;
    QSet<QString> m_selected;
    QMap<QString, QPoint> m_positions;
    QString m_root, m_profile, m_directory;
    FileOperations m_operations;
    QFileSystemWatcher m_watcher;
    QTimer m_drives;
    QStringList m_driveNames;
    bool m_trashFull = false;
    QTimer m_refresh;
    QSize m_viewport{1120, 760};
    bool m_busy = false;
    QString m_operationStatus;
    qulonglong m_selectionEpoch = 0;
    QMap<QString, QPoint> m_dragPositions;
    QString m_dropTarget;
    std::optional<DisplayLayout> m_displayLayout;
    QSet<QString> m_automaticPositions;
    QStringList m_windowDropPaths;
    bool m_windowDropCopy = false;
    bool m_selecting = false;
    QPoint m_selectionOrigin, m_selectionPointer;
    QSet<QString> m_selectionBefore, m_selectionBase;
};
