#pragma once
#include <QMenu>
#include "file_operations.hpp"
#include <QFileSystemModel>
#include <QListView>
#include <QMap>
#include <QPointer>
#include <QMainWindow>
#include <QSettings>
#include <QStackedWidget>
#include <QTreeView>
#include <functional>

class FolderIcons : public QListView
{
    Q_OBJECT
  public:
    explicit FolderIcons(QWidget *parent = nullptr);
    void restorePosition(const QModelIndex &index, QPoint point)
    {
        setPositionForIndex(point, index);
    }
  signals:
    void filesDropped(QStringList sources, QString destination, bool copy);
    void placementChanged();

  protected:
    void startDrag(Qt::DropActions actions) override;
    void dragEnterEvent(QDragEnterEvent *event) override;
    void dragMoveEvent(QDragMoveEvent *event) override;
    void dropEvent(QDropEvent *event) override;
};
class FolderList final : public QTreeView
{
    Q_OBJECT
  public:
    explicit FolderList(QWidget *parent = nullptr);
  signals:
    void filesDropped(QStringList sources, QString destination, bool copy);

  protected:
    void startDrag(Qt::DropActions) override;
    void dragEnterEvent(QDragEnterEvent *) override;
    void dragMoveEvent(QDragMoveEvent *) override;
    void dropEvent(QDropEvent *) override;
};
class FinderWindow final : public QMainWindow
{
    Q_OBJECT
  public:
    FinderWindow(QString root, QString profile, QString directory, QWidget *parent = nullptr);
    QString directory() const
    {
        return m_directory;
    }
    void setViewMode(bool list);
    // Names starting with a dot: shown in every Finder window or in none (View > Show Hidden
    // Files), and remembered in the profile's finder.ini.
    void setShowHidden(bool show);
    QVariantMap directoryState() const;
    QString dropDirectoryAt(QPoint windowPosition) const;
    static bool finishFileDrag(const QString &token);
    // File > Find…: names containing the text, in this folder and below (bounded, background).
    void findItems(const QString &text);
    // Special ▸ Sleep, Restart and Shut Down appear only when the desktop session offers them.
    void setPowerAvailable(bool available);

  signals:
    void openFolder(QString path);
    void openDocument(QString path, bool chooseApplication);
    void windowMenuRequested(QMenu *menu);
    // Special ▸ Log Out, Sleep, Restart or Shut Down: "log-out", "sleep", "restart", "shut-down".
    void sessionRequested(QString command);

  protected:
    void closeEvent(QCloseEvent *event) override;

  private:
    QStringList selectedPaths() const;
    QStringList selectedNames() const;
    void openIndex(const QModelIndex &index);
    // With bytes, a copy: a determinate dialog ("1.2 GB of 4.5 GB") whose Stop sets cancel.
    void perform(QString label, std::function<FileResult()> job, std::shared_ptr<CopyProgress> bytes = {},
                 std::shared_ptr<std::atomic_bool> cancel = {});
    void saveState();
    void restorePositions();
    void updateActions();
    void showContextMenu(QAbstractItemView *view, QPoint position);
    void showInfo(const QString &path);
    QString m_root, m_profile, m_directory, m_stateKey;
    FileOperations m_operations;
    QFileSystemModel *m_model;
    FolderIcons *m_icons;
    FolderList *m_list;
    QStackedWidget *m_views;
    std::unique_ptr<QSettings> m_settings;
    bool m_busy = false;
    bool m_directoryReady = false;
    // Open Get Info windows by item path: asking again brings the same window forward.
    QMap<QString, QPointer<QWidget>> m_infoWindows;
    QList<QAction *> m_powerActions;
};
