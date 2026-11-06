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
#include <QTimer>
#include <QTreeView>
#include <QPersistentModelIndex>
#include <functional>

// Spring-loaded folders (Mac OS 9): a drag held over a folder for a second opens it, so the
// drop can go further in. Moving off the folder, leaving the window or dropping cancels it.
class SpringLoading
{
  public:
    explicit SpringLoading(QAbstractItemView *view);
    void hover(QPoint at);
    void cancel();
    std::function<void(QString)> opened;
    static constexpr int delay = 1000;

  private:
    QAbstractItemView *m_view;
    QTimer m_timer;
    QPersistentModelIndex m_target;
};
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
    void springOpen(QString folder);

  protected:
    void startDrag(Qt::DropActions actions) override;
    void dragEnterEvent(QDragEnterEvent *event) override;
    void dragMoveEvent(QDragMoveEvent *event) override;
    void dragLeaveEvent(QDragLeaveEvent *event) override;
    void dropEvent(QDropEvent *event) override;

  private:
    SpringLoading m_spring{this};
};
class FolderList final : public QTreeView
{
    Q_OBJECT
  public:
    explicit FolderList(QWidget *parent = nullptr);
  signals:
    void filesDropped(QStringList sources, QString destination, bool copy);
    void springOpen(QString folder);

  protected:
    void startDrag(Qt::DropActions) override;
    void dragEnterEvent(QDragEnterEvent *) override;
    void dragMoveEvent(QDragMoveEvent *) override;
    void dragLeaveEvent(QDragLeaveEvent *) override;
    void dropEvent(QDropEvent *) override;

  private:
    SpringLoading m_spring{this};
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
    // Shows another folder in this window, as Dolphin does (Back returns). record: add to history.
    bool navigate(const QString &path, bool record = true);

  signals:
    void openFolder(QString path);
    // This window now shows another folder.
    void navigated(QString from, QString to);
    void openDocument(QString path, bool chooseApplication);
    void windowMenuRequested(QMenu *menu);
    // Special ▸ Log Out, Sleep, Restart or Shut Down: "log-out", "sleep", "restart", "shut-down".
    void sessionRequested(QString command);

  protected:
    void closeEvent(QCloseEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;

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
    // Paste the clipboard's files (copied or cut here, in another Finder or another file manager).
    void pasteInto(const QString &destination);
    std::function<void(QStringList, QString, bool)> m_receiveFiles;
    void showInfo(const QString &path);
    // Label ▸ for the selected items (0: None).
    void setLabel(int label);
    void updateTitle();
    void updateHeader();
    void updatePlaces();
    QStringList m_back, m_forward;
    class QLabel *m_header = nullptr;
    class QLineEdit *m_path = nullptr;
    class QListWidget *m_places = nullptr;
    class QToolButton *m_backButton = nullptr, *m_forwardButton = nullptr, *m_upButton = nullptr;
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
