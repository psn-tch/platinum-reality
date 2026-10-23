#include "finder_window.hpp"
#include <QStandardPaths>
#include <QLocale>
#include <QTimer>
#include <QPushButton>
#include <QTreeWidget>
#include <QThread>
#include <QDirIterator>
#include "clipboard_window.hpp"
#include <QClipboard>
#include <QGuiApplication>
#include "file_details.hpp"
#include "file_drag.hpp"
#include "platinum_style.hpp"
#include "window_surface_id.hpp"
#include <QActionGroup>
#include <QApplication>
#include <QCloseEvent>
#include <QCryptographicHash>
#include <QDrag>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QFileDialog>
#include <QFileIconProvider>
#include <QFormLayout>
#include <QFrame>
#include <QFutureWatcher>
#include <QHeaderView>
#include <QInputDialog>
#include <QLabel>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QMimeData>
#include <QPainter>
#include <QPointer>
#include <QProgressDialog>
#include <QScrollBar>
#include <QShortcut>
#include <QSet>
#include <QStorageInfo>
#include <QTimer>
#include <QUuid>
#include <QVBoxLayout>
#include <QtConcurrent>
#include <algorithm>

FolderIcons::FolderIcons(QWidget *parent) : QListView(parent)
{
    setViewMode(QListView::IconMode);
    setIconSize({32, 32});
    setGridSize({96, 78});
    setResizeMode(QListView::Adjust);
    setMovement(QListView::Free);
    setSelectionMode(QAbstractItemView::ExtendedSelection);
    setSelectionRectVisible(true);
    setEditTriggers(QAbstractItemView::NoEditTriggers);
    setDragEnabled(true);
    setAcceptDrops(true);
    setDropIndicatorShown(true);
    setSpacing(8);
}
namespace
{
QPointer<QDrag> activeFileDrag;
void startFileDrag(QAbstractItemView *view, const QModelIndexList &indexes)
{
    auto *model = qobject_cast<QFileSystemModel *>(view->model());
    if (!model)
        return;
    QList<QUrl> urls;
    QSet<QString> seen;
    for (const auto &index : indexes)
    {
        const auto path = model->filePath(index.siblingAtColumn(0));
        if (!seen.contains(path))
        {
            seen.insert(path);
            urls.push_back(QUrl::fromLocalFile(path));
        }
    }
    if (urls.isEmpty() || urls.size() > 128)
        return;
    auto *mime = new QMimeData;
    mime->setUrls(urls);
    const auto token = QUuid::createUuid().toString(QUuid::WithoutBraces);
    mime->setData(finderDragMime, token.toUtf8());
    mime->setData(finderFileUrlsMime, mime->data("text/uri-list"));
    QPointer<QDrag> drag = new QDrag(view);
    drag->setProperty("frankensteinFileDragToken", token);
    activeFileDrag = drag;
    drag->setMimeData(mime);
    QPixmap preview(160, 40);
    preview.fill(Qt::transparent);
    QPainter painter(&preview);
    painter.setPen(Qt::black);
    painter.setBrush(QColor(238, 238, 238, 230));
    painter.drawRect(0, 0, 159, 39);
    painter.drawPixmap(4, 4, model->fileIcon(indexes.first()).pixmap(32, 32));
    const auto label = urls.size() == 1 ? QFileInfo(urls.first().toLocalFile()).fileName()
                                        : QString("%1 items").arg(urls.size());
    painter.drawText(QRect(42, 4, 113, 32), Qt::AlignVCenter,
                     painter.fontMetrics().elidedText(label, Qt::ElideRight, 113));
    painter.end();
    drag->setPixmap(preview);
    drag->setHotSpot({12, 12});
    // The destination performs the move. Never delete originals based on the
    // protocol result: the asynchronous file job can still fail or collide.
    drag->exec(Qt::CopyAction | Qt::MoveAction, Qt::MoveAction);
    if (activeFileDrag == drag)
        activeFileDrag.clear();
    if (drag)
        drag->deleteLater();
}
Qt::DropAction fileDropAction(const QDropEvent *event)
{
    const bool copy = event->modifiers().testFlag(Qt::AltModifier) ||
                      event->modifiers().testFlag(Qt::ControlModifier) ||
                      !event->mimeData()->hasFormat(finderDragMime);
    if (copy && event->possibleActions().testFlag(Qt::CopyAction))
        return Qt::CopyAction;
    if (!copy && event->possibleActions().testFlag(Qt::MoveAction))
        return Qt::MoveAction;
    return event->possibleActions().testFlag(Qt::CopyAction) ? Qt::CopyAction : Qt::IgnoreAction;
}
void acceptFileDrag(QDropEvent *event)
{
    const auto action = fileDropAction(event);
    if (action != Qt::IgnoreAction && event->mimeData()->hasUrls())
    {
        event->setDropAction(action);
        event->accept();
    }
    else
        event->ignore();
}
QString dropDirectory(QAbstractItemView *view, QPoint point)
{
    auto *model = qobject_cast<QFileSystemModel *>(view->model());
    const auto index = view->indexAt(point).siblingAtColumn(0);
    return index.isValid() && model->isDir(index) ? model->filePath(index)
                                                  : model->filePath(view->rootIndex());
}
} // namespace
bool FinderWindow::finishFileDrag(const QString &token)
{
    if (token.isEmpty() || token.size() > 64 || !activeFileDrag ||
        activeFileDrag->property("frankensteinFileDragToken").toString() != token)
        return false;
    // The desktop has already decoded the URLs. Older data-device protocols
    // may not notify the source that another process finished its drop. Cancel
    // only this matching transport loop; the destination owns the file job.
    QDrag::cancel();
    return true;
}
void FolderIcons::startDrag(Qt::DropActions)
{
    startFileDrag(this, selectedIndexes());
}
void FolderIcons::dragEnterEvent(QDragEnterEvent *e)
{
    acceptFileDrag(e);
}
void FolderIcons::dragMoveEvent(QDragMoveEvent *e)
{
    acceptFileDrag(e);
}
void FolderIcons::dropEvent(QDropEvent *e)
{
    auto *model = qobject_cast<QFileSystemModel *>(this->model());
    const auto target = indexAt(e->position().toPoint());
    const bool folder = target.isValid() && model->isDir(target);
    if (e->source() == this && !folder &&
        !(e->modifiers() & (Qt::AltModifier | Qt::ControlModifier)))
    {
        QPoint position = e->position().toPoint() +
                          QPoint(horizontalScrollBar()->value(), verticalScrollBar()->value());
        for (const auto &index : selectedIndexes())
        {
            if (index.column() != 0)
                continue;
            setPositionForIndex(position, index);
            position += QPoint(15, 15);
        }
        emit placementChanged();
        e->setDropAction(Qt::MoveAction);
        e->accept();
        return;
    }
    const auto paths = fileDragPaths(e->mimeData());
    if (paths.isEmpty())
    {
        e->ignore();
        return;
    }
    acceptFileDrag(e);
    if (e->isAccepted())
        emit filesDropped(paths, dropDirectory(this, e->position().toPoint()),
                          e->dropAction() == Qt::CopyAction);
}
FolderList::FolderList(QWidget *parent) : QTreeView(parent)
{
    setDragEnabled(true);
    setAcceptDrops(true);
    setDropIndicatorShown(true);
    setDefaultDropAction(Qt::MoveAction);
}
void FolderList::startDrag(Qt::DropActions)
{
    startFileDrag(this, selectedIndexes());
}
void FolderList::dragEnterEvent(QDragEnterEvent *e)
{
    acceptFileDrag(e);
}
void FolderList::dragMoveEvent(QDragMoveEvent *e)
{
    acceptFileDrag(e);
}
void FolderList::dropEvent(QDropEvent *e)
{
    const auto paths = fileDragPaths(e->mimeData());
    if (paths.isEmpty())
    {
        e->ignore();
        return;
    }
    acceptFileDrag(e);
    if (e->isAccepted())
        emit filesDropped(paths, dropDirectory(this, e->position().toPoint()),
                          e->dropAction() == Qt::CopyAction);
}
FinderWindow::FinderWindow(QString root, QString profile, QString directory, QWidget *parent)
    : QMainWindow(parent), m_root(std::move(root)), m_profile(std::move(profile)),
      m_directory(QDir::cleanPath(std::move(directory))),
      m_operations(m_root, FinderPlaces::trashFor(m_profile), FinderPlaces::volumes), m_model(new QFileSystemModel(this)),
      m_icons(new FolderIcons), m_list(new FolderList), m_views(new QStackedWidget)
{
    setAttribute(Qt::WA_DeleteOnClose);
    setWindowTitle(m_directory == m_operations.trashBase() + "/files" ? "Trash"
                   : m_directory == m_root                            ? FinderPlaces::diskName(m_profile)
                   : QFileInfo(m_directory).fileName().isEmpty()
                       ? FinderPlaces::diskName(m_profile)
                       : QFileInfo(m_directory).fileName());
    setObjectName("finderWindow");
    QDir().mkpath(m_profile);
    m_settings = std::make_unique<QSettings>(m_profile + "/finder.ini", QSettings::IniFormat);
    m_stateKey = QString::fromLatin1(
        QCryptographicHash::hash(m_directory.toUtf8(), QCryptographicHash::Sha256).toHex());
    m_settings->beginGroup(m_stateKey);
    resize(640, 430);
    restoreGeometry(m_settings->value("geometry").toByteArray());
    auto *content = new QWidget;
    auto *layout = new QVBoxLayout(content);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    // Mac OS 9 folder window header: "19 items, 47.32 GB available", centred
    // (docs/historical-fidelity-review.md F05). No path or status bar below.
    const auto header = [directory = m_directory](int count)
    {
        auto text = QString("%1 item%2").arg(count).arg(count == 1 ? "" : "s");
        const QStorageInfo volume(directory);
        if (volume.isValid() && volume.isReady())
            text += ", " + QLocale::system().formattedDataSize(volume.bytesAvailable(), 2,
                                                                QLocale::DataSizeTraditionalFormat) +
                    " available";
        return text;
    };
    auto *info = new QLabel(
        header(QDir(m_directory).entryList(QDir::AllEntries | QDir::NoDotAndDotDot).size()));
    info->setObjectName("folderItemCount");
    info->setAlignment(Qt::AlignCenter);
    info->setFrameStyle(QFrame::Panel | QFrame::Sunken);
    info->setMinimumHeight(23);
    layout->addWidget(info);
    installPlatinumIcons(m_model);
    m_model->setReadOnly(true);
    m_model->setFilter(QSettings(m_profile + "/finder.ini", QSettings::IniFormat).value("showHiddenFiles", false).toBool()
                           ? QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden
                           : QDir::AllEntries | QDir::NoDotAndDotDot);
    m_model->setRootPath(m_directory);
    m_icons->setObjectName("iconView");
    m_list->setObjectName("listView");
    m_icons->setModel(m_model);
    m_list->setModel(m_model);
    const auto rootIndex = m_model->index(m_directory);
    m_icons->setRootIndex(rootIndex);
    m_list->setRootIndex(rootIndex);
    const auto updateCount = [this, info, header]
    {
        info->setText(header(m_model->rowCount(m_icons->rootIndex())));
    };
    connect(m_model, &QFileSystemModel::rowsInserted, this, updateCount);
    connect(m_model, &QFileSystemModel::rowsRemoved, this, updateCount);
    connect(m_model, &QFileSystemModel::modelReset, this, updateCount);
    connect(m_model, &QFileSystemModel::directoryLoaded, this,
            [this, updateCount](const QString &path)
            {
                if (path == m_directory)
                    updateCount();
            });
    m_list->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_list->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_list->setSortingEnabled(true);
    m_list->sortByColumn(0, Qt::AscendingOrder);
    m_list->setColumnWidth(0, 230);
    m_views->addWidget(m_icons);
    m_views->addWidget(m_list);
    layout->addWidget(m_views);
    setCentralWidget(content);
    setViewMode(m_settings->value("list", false).toBool());
    connect(m_icons, &QListView::doubleClicked, this, &FinderWindow::openIndex);
    connect(m_list, &QTreeView::doubleClicked, this, &FinderWindow::openIndex);
    connect(m_model, &QFileSystemModel::directoryLoaded, this,
            [this](const QString &path)
            {
                if (path == m_directory)
                {
                    m_directoryReady = true;
                    restorePositions();
                }
            });
    connect(m_icons, &FolderIcons::placementChanged, this, &FinderWindow::saveState);
    const auto receiveFiles = [this](QStringList paths, QString destination, bool copy)
    {
        if (paths.isEmpty())
            return;
        const auto ops = m_operations;
        auto bytes = copy ? std::make_shared<CopyProgress>() : std::shared_ptr<CopyProgress>();
        auto cancel = std::make_shared<std::atomic_bool>(false);
        perform(copy ? "Copying…" : "Moving…",
                [ops, paths, destination, copy, bytes, cancel]
                {
                    if (bytes)
                        bytes->total.store(FileOperations::measure(paths));
                    FileResult result{true, {}, {}};
                    int completed = 0;
                    for (const auto &path : paths)
                    {
                        result = copy ? ops.copy(path, destination, cancel, bytes) : ops.move(path, destination);
                        if (!result.ok)
                        {
                            result.error = QString("%1 of %2 items completed.\n\n")
                                               .arg(completed)
                                               .arg(paths.size()) +
                                           result.error;
                            break;
                        }
                        ++completed;
                    }
                    return result;
                }, bytes, cancel);
    };
    connect(m_icons, &FolderIcons::filesDropped, this, receiveFiles);
    connect(m_list, &FolderList::filesDropped, this, receiveFiles);
    // Paste follows the clipboard (files copied here, in another Finder or another file manager).
    connect(QGuiApplication::clipboard(), &QClipboard::dataChanged, this, &FinderWindow::updateActions);
    auto action = [this](QMenu *menu, QString text, QString name, QKeySequence shortcut, auto fn)
    {
        auto *a = menu->addAction(text);
        a->setObjectName(name);
        setClassicShortcuts(a, shortcut);
        connect(a, &QAction::triggered, this, fn);
        return a;
    };
    auto *file = menuBar()->addMenu("File");
    action(file, "New Folder", "newFolder", QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_N),
           [this]
           {
               bool ok = false;
               auto name = QInputDialog::getText(this, "New Folder", "Name:", QLineEdit::Normal,
                                                 "untitled folder", &ok);
               if (ok)
               {
                   auto ops = m_operations;
                   auto dir = m_directory;
                   perform("Creating folder…",
                           [ops, dir, name] { return ops.createFolder(dir, name); });
               }
           });
    action(file, "Open", "open", QKeySequence(Qt::CTRL | Qt::Key_O),
           [this]
           {
               for (const auto &p : selectedPaths())
                   openIndex(m_model->index(p));
           });
    action(file, "Close Window", "closeWindow", QKeySequence(Qt::CTRL | Qt::Key_W),
           [this] { close(); });
    action(file, "Open With…", "openWith", {},
           [this]
           {
               const auto paths = selectedPaths();
               if (paths.size() == 1 && m_operations.readableFile(paths.first()))
                   emit openDocument(paths.first(), true);
           });
    file->addSeparator();
    action(
        file, "Get Info…", "getInfo", QKeySequence(Qt::CTRL | Qt::Key_I),
        [this]
        {
            const auto paths = selectedPaths();
            if (paths.size() == 1)
                showInfo(paths.first());
        });
    action(file, "Duplicate", "duplicate", QKeySequence(Qt::CTRL | Qt::Key_D),
           [this]
           {
               auto p = selectedPaths();
               if (p.size() == 1)
               {
                   auto ops = m_operations;
                   perform("Duplicating…", [ops, path = p.first()] { return ops.duplicate(path); });
               }
           });
    action(file, "Find…", "find", QKeySequence(Qt::CTRL | Qt::Key_F),
           [this]
           {
               bool ok = false;
               const auto text = QInputDialog::getText(this, "Find", "Find items whose name contains:",
                                                       QLineEdit::Normal, {}, &ok).trimmed();
               if (ok && !text.isEmpty())
                   findItems(text);
           });
    action(file, "Rename…", "rename", QKeySequence(Qt::Key_F2),
           [this]
           {
               auto p = selectedPaths();
               if (p.size() != 1)
                   return;
               bool ok = false;
               auto name = QInputDialog::getText(this, "Rename", "Name:", QLineEdit::Normal,
                                                 QFileInfo(p.first()).fileName(), &ok);
               if (ok)
               {
                   auto ops = m_operations;
                   perform("Renaming…",
                           [ops, path = p.first(), name] { return ops.rename(path, name); });
               }
           });
    file->addSeparator();
    action(file, "Move to Trash", "trash", QKeySequence(Qt::CTRL | Qt::Key_Backspace),
           [this]
           {
               auto p = selectedPaths();
               if (p.isEmpty())
                   return;
               auto ops = m_operations;
               perform("Moving to Trash…",
                       [ops, p]
                       {
                           FileResult r{true, {}, {}};
                           for (auto path : p)
                           {
                               r = ops.trash(path);
                               if (!r.ok)
                                   break;
                           }
                           return r;
                       });
           });
    action(file, "Put Away", "restore", QKeySequence(Qt::CTRL | Qt::Key_Y),
           [this]
           {
               auto p = selectedPaths();
               if (p.size() != 1)
                   return;
               auto ops = m_operations;
               perform("Putting away…", [ops, path = p.first()] { return ops.restore(path); });
           });
    // Mac OS 9's Finder has no Quit command (docs/historical-fidelity-review.md F07).
    // Edit as in the 9.2.2 Finder (checked in psn_tch's 9.2.2 VM): with items selected, Copy copies
    // their names as text, one per line in display order. Native adaptation: the items themselves
    // travel too (text/uri-list, and the GNOME and KDE copy/cut markers), so Cut, Copy and Paste
    // work with other file managers; Paste copies, or after Cut moves, into this folder.
    auto *edit = menuBar()->addMenu("Edit");
    const auto unavailable = [&](QString text, QString name, QKeySequence shortcut)
    {
        auto *a = action(edit, text, name, shortcut, [] {});
        a->setEnabled(false);
    };
    unavailable("Undo", "undo", QKeySequence::Undo);
    edit->addSeparator();
    const auto putOnClipboard = [this](bool cut)
    {
        const auto paths = selectedPaths();
        if (paths.isEmpty())
            return;
        auto *mime = new QMimeData;
        QList<QUrl> urls;
        QByteArray gnome(cut ? "cut" : "copy");
        for (const auto &path : paths)
        {
            urls << QUrl::fromLocalFile(path);
            gnome += "\n" + QUrl::fromLocalFile(path).toEncoded();
        }
        mime->setUrls(urls);
        mime->setText(selectedNames().join('\n'));
        mime->setData("x-special/gnome-copied-files", gnome);
        mime->setData("application/x-kde-cutselection", cut ? "1" : "0");
        QGuiApplication::clipboard()->setMimeData(mime);
    };
    action(edit, "Cut", "cut", QKeySequence::Cut, [putOnClipboard] { putOnClipboard(true); });
    action(edit, "Copy", "copy", QKeySequence::Copy, [putOnClipboard] { putOnClipboard(false); });
    action(edit, "Paste", "paste", QKeySequence::Paste,
           [this, receiveFiles]
           {
               const auto *mime = QGuiApplication::clipboard()->mimeData();
               if (!mime || !mime->hasUrls())
                   return;
               QStringList paths;
               for (const auto &url : mime->urls())
                   if (url.isLocalFile())
                       paths << url.toLocalFile();
               const bool cut = mime->data("application/x-kde-cutselection") == "1" ||
                                mime->data("x-special/gnome-copied-files").startsWith("cut\n");
               receiveFiles(paths, m_directory, !cut);
               if (cut)
                   QGuiApplication::clipboard()->clear(); // A moved item cannot be pasted again.
           });
    unavailable("Clear", "clear", {});
    action(edit, "Select All", "selectAll", QKeySequence::SelectAll,
           [this]
           {
               if (m_views->currentIndex() == 0)
                   m_icons->selectAll();
               else
                   m_list->selectAll();
           });
    action(edit, "Show Clipboard", "showClipboard", {}, [this] { ClipboardWindow::show(this); });
    auto *view = menuBar()->addMenu("View");
    action(view, "as Icons", "icons", {}, [this] { setViewMode(false); });
    action(view, "as List", "list", {}, [this] { setViewMode(true); });
    view->addSeparator();
    auto *hidden = action(view, "Show Hidden Files", "showHidden", QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_Period),
                          [this] { setShowHidden(!(m_model->filter() & QDir::Hidden)); });
    hidden->setCheckable(true);
    hidden->setChecked(m_model->filter() & QDir::Hidden);
    view->addSeparator();
    action(view, "Clean Up", "cleanUp", {},
           [this]
           {
               m_icons->setMovement(QListView::Static);
               m_icons->doItemsLayout();
               m_icons->setMovement(QListView::Free);
               saveState();
           });
    // Go: a native adaptation (Mac OS 9 reached places through the desktop, the Apple menu and
    // Favorites). The user's standard folders and mounted drives, and a typed path, each opened
    // in its own window only when it is readable inside the places this Finder may work in.
    auto *go = menuBar()->addMenu("Go");
    const auto place = [this](const QString &path)
    {
        if (m_operations.readableDirectory(path))
            emit openFolder(QDir::cleanPath(path));
        else
            QMessageBox::information(this, "Go", "“" + QDir::toNativeSeparators(path) +
                                                     "” is not available.");
    };
    action(go, FinderPlaces::diskName(m_profile), "goHome", QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_H),
           [this, place] { place(m_root); });
    action(go, "Desktop", "goDesktop", QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_D),
           [this, place] { place(m_root + "/Desktop"); });
    const QList<std::pair<QString, QStandardPaths::StandardLocation>> standard{
        {"Documents", QStandardPaths::DocumentsLocation}, {"Downloads", QStandardPaths::DownloadLocation},
        {"Pictures", QStandardPaths::PicturesLocation}, {"Music", QStandardPaths::MusicLocation},
        {"Videos", QStandardPaths::MoviesLocation}};
    QList<QAction *> folders;
    for (const auto &[label, location] : standard)
        folders << action(go, label, "go" + label, {}, [place, location]
                          { place(QStandardPaths::writableLocation(location)); });
    auto *drivesStart = go->addSeparator();
    auto *drivesEnd = go->addSeparator();
    action(go, "Go to Folder…", "goToFolder", QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_G),
           [this, place]
           {
               bool ok = false;
               auto path = QInputDialog::getText(this, "Go to Folder", "Folder:", QLineEdit::Normal,
                                                 QDir::toNativeSeparators(m_directory), &ok).trimmed();
               if (!ok || path.isEmpty())
                   return;
               if (path == "~" || path.startsWith("~/"))
                   path = QDir::homePath() + path.mid(1);
               place(QDir(m_directory).absoluteFilePath(path));
           });
    // Folders that are not there are hidden; drives are listed as they are mounted now.
    connect(go, &QMenu::aboutToShow, this,
            [this, go, folders, standard, drivesStart, drivesEnd, place]
            {
                for (qsizetype i = 0; i < folders.size(); ++i)
                    folders[i]->setVisible(
                        m_operations.readableDirectory(QStandardPaths::writableLocation(standard[i].second)));
                const auto actions = go->actions();
                for (auto i = actions.indexOf(drivesStart) + 1; i < actions.indexOf(drivesEnd); ++i)
                {
                    go->removeAction(actions[i]);
                    actions[i]->deleteLater();
                }
                QList<QAction *> drives;
                for (const auto &volumes : FinderPlaces::volumes)
                    for (const auto &drive : QDir(volumes).entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name))
                        if (drives.size() < 32 && m_operations.readableDirectory(drive.absoluteFilePath()))
                        {
                            auto *entry = new QAction(drive.fileName(), go);
                            entry->setObjectName("goDrive");
                            connect(entry, &QAction::triggered, this, [place, path = drive.absoluteFilePath()] { place(path); });
                            drives << entry;
                        }
                go->insertActions(drivesEnd, drives);
                drivesEnd->setVisible(!drives.isEmpty());
            });
    auto *window = menuBar()->addMenu("Window");
    action(window, "Open Parent", "parent", QKeySequence(Qt::CTRL | Qt::Key_Up),
           [this]
           {
               auto p = QFileInfo(m_directory).absolutePath();
               if (m_operations.readableDirectory(p))
                   emit openFolder(p);
           });
    // The open Finder windows, rebuilt each time the menu opens (Mac OS 9.1 Window menu).
    auto *windowList = window->addSeparator();
    connect(window, &QMenu::aboutToShow, this,
            [this, window, windowList]
            {
                const auto actions = window->actions();
                for (auto i = actions.indexOf(windowList) + 1; i < actions.size(); ++i)
                {
                    window->removeAction(actions[i]);
                    actions[i]->deleteLater();
                }
                emit windowMenuRequested(window);
            });
    auto *special = menuBar()->addMenu("Special");
    action(special, "Empty Trash…", "emptyTrash", {},
           [this]
           {
               const auto items = m_operations.trashedItems();
               if (items.isEmpty())
               {
                   QMessageBox::information(this, "Empty Trash", "The Trash is empty.");
                   return;
               }
               QMessageBox ask(QMessageBox::Warning, "Empty Trash",
                               QString("The Trash contains %1 item%2. Are you sure you want to "
                                       "permanently remove %3?")
                                   .arg(items.size())
                                   .arg(items.size() == 1 ? "" : "s")
                                   .arg(items.size() == 1 ? "this item" : "these items"),
                               QMessageBox::Ok | QMessageBox::Cancel, this);
               ask.setDefaultButton(QMessageBox::Cancel); // Destructive: Cancel is the default.
               if (ask.exec() != QMessageBox::Ok)
                   return;
               auto ops = m_operations;
               perform("Emptying Trash…",
                       [ops, items]
                       {
                           for (const auto &item : items)
                               if (auto result = ops.removeFromTrash(item); !result.ok)
                                   return result;
                           return FileResult{true, ops.trashBase(), {}};
                       });
           });
    // As in the 9.2.2 Finder (Eject and Erase Disk aside). Only inside the desktop session, which
    // performs them: Log Out closes applications first; Sleep, Restart and Shut Down where offered.
    if (qEnvironmentVariable("FRANKENSTEIN_ISOLATED") == "1")
    {
        special->addSeparator();
        m_powerActions << action(special, "Sleep", "sleep", {}, [this] { emit sessionRequested("sleep"); })
                       << action(special, "Restart", "restart", {}, [this] { emit sessionRequested("restart"); })
                       << action(special, "Shut Down", "shutDown", {}, [this] { emit sessionRequested("shut-down"); })
                       << special->addSeparator();
        action(special, "Log Out", "logOut", {}, [this] { emit sessionRequested("log-out"); });
        setPowerAvailable(false);
    }
    auto *help = menuBar()->addMenu("Help");
    action(help, "About Finder…", "about", {},
           [this]
           {
               QMessageBox::about(
                   this, "Finder",
                   "Platinum Reality\nA native Linux spatial file manager.\n\nHistorical "
                   "behavior is being qualified against Mac OS 9.2.2.");
           });
    for (auto *view :
         {static_cast<QAbstractItemView *>(m_icons), static_cast<QAbstractItemView *>(m_list)})
    {
        view->setContextMenuPolicy(Qt::CustomContextMenu);
        connect(view, &QWidget::customContextMenuRequested, this,
                [this, view](QPoint point) { showContextMenu(view, point); });
        connect(view->selectionModel(), &QItemSelectionModel::selectionChanged, this,
                &FinderWindow::updateActions);
    }
    connect(file, &QMenu::aboutToShow, this, &FinderWindow::updateActions);
    updateActions();
    QTimer::singleShot(100, this, &FinderWindow::restorePositions);
}
QStringList FinderWindow::selectedNames() const
{
    // In the order the window shows them: icons by position, list rows top to bottom.
    const bool icons = m_views->currentIndex() == 0;
    auto *view = icons ? static_cast<QAbstractItemView *>(m_icons) : static_cast<QAbstractItemView *>(m_list);
    QModelIndexList selected;
    QSet<QString> seen;
    for (const auto &index : view->selectionModel()->selectedIndexes())
    {
        const auto item = index.siblingAtColumn(0);
        if (!seen.contains(m_model->filePath(item)))
        {
            seen.insert(m_model->filePath(item));
            selected.push_back(item);
        }
    }
    std::stable_sort(selected.begin(), selected.end(), [view](const QModelIndex &a, const QModelIndex &b) {
        const auto first = view->visualRect(a), second = view->visualRect(b);
        return first.y() != second.y() ? first.y() < second.y() : first.x() < second.x();
    });
    QStringList names;
    for (const auto &index : selected)
        names.push_back(m_model->fileName(index));
    return names;
}
QStringList FinderWindow::selectedPaths() const
{
    QStringList paths;
    auto *selection =
        m_views->currentIndex() == 0 ? m_icons->selectionModel() : m_list->selectionModel();
    QSet<QString> seen;
    // An icon view exposes column 0 of a four-column QFileSystemModel. A
    // selected icon need not constitute a fully selected model row.
    for (const auto &i : selection->selectedIndexes())
    {
        const auto path = m_model->filePath(i.siblingAtColumn(0));
        if (!seen.contains(path))
        {
            paths.push_back(path);
            seen.insert(path);
        }
    }
    return paths;
}
void FinderWindow::updateActions()
{
    const auto paths = selectedPaths();
    const auto canonical = QFileInfo(m_directory).canonicalFilePath();
    const bool writable =
        !canonical.isEmpty() && (canonical == m_root || canonical.startsWith(m_root + '/'));
    const bool trash = m_directory == m_operations.trashBase() + "/files";
    const auto enable = [this](const char *name, bool enabled)
    {
        if (auto *action = findChild<QAction *>(name))
            action->setEnabled(enabled && !m_busy);
    };
    enable("newFolder", writable);
    enable("open", !paths.isEmpty());
    enable("openWith", paths.size() == 1 && m_operations.readableFile(paths.first()));
    enable("getInfo", paths.size() == 1);
    enable("copy", !paths.isEmpty());
    enable("cut", !paths.isEmpty() && m_directory != m_operations.trashBase() + "/files");
    const auto *clipboard = QGuiApplication::clipboard()->mimeData();
    enable("paste", clipboard && clipboard->hasUrls() && m_directory != m_operations.trashBase() + "/files");
    enable("rename", writable && paths.size() == 1);
    enable("duplicate", writable && paths.size() == 1);
    enable("trash", writable && !paths.isEmpty());
    enable("restore", trash && paths.size() == 1);
    enable("cleanUp", m_views->currentIndex() == 0);
}
void FinderWindow::showInfo(const QString &path)
{
    // Mac OS 9's Info window (Finishing School FS-G01): modeless, one per item, bound to that
    // item and read-only. It belongs to the Finder, not this folder window, so it stays open
    // when the folder closes.
    if (const auto open = m_infoWindows.value(path))
    {
        open->raise();
        open->activateWindow();
        return;
    }
    const QFileInfo item(path);
    const auto details = fileDetails(path);
    auto *window = new QWidget(nullptr, Qt::Window);
    window->setAttribute(Qt::WA_DeleteOnClose);
    window->setObjectName("infoWindow");
    window->setWindowTitle(item.fileName() + " Info");
    auto *layout = new QVBoxLayout(window);
    auto *heading = new QHBoxLayout;
    auto *icon = new QLabel;
    icon->setPixmap(QFileIconProvider().icon(item).pixmap(32, 32));
    auto *name = new QLabel(item.fileName());
    name->setObjectName("infoName");
    name->setTextFormat(Qt::PlainText);
    auto bold = name->font();
    bold.setBold(true);
    name->setFont(bold);
    heading->addWidget(icon);
    heading->addWidget(name, 1);
    layout->addLayout(heading);
    auto *rule = new QFrame;
    rule->setFrameShape(QFrame::HLine);
    rule->setFrameShadow(QFrame::Sunken);
    layout->addWidget(rule);
    auto *facts = new QFormLayout;
    facts->setLabelAlignment(Qt::AlignRight);
    const bool alias = details.contains("original");
    const QList<std::pair<QString, QString>> rows{{"Kind", "kindLabel"},
                                                  {alias ? "Original" : "Size", alias ? "original" : "size"},
                                                  {"Where", "where"},
                                                  {"Created", "created"},
                                                  {"Modified", "modified"}};
    for (const auto &[label, key] : rows)
    {
        auto *value = new QLabel(details.value(key).toString());
        value->setObjectName("info" + label);
        value->setTextFormat(Qt::PlainText);
        value->setWordWrap(true);
        value->setTextInteractionFlags(Qt::TextSelectableByMouse);
        facts->addRow(label + ":", value);
    }
    layout->addLayout(facts);
    // Escape or Close Window (Ctrl+W, the Finder's Command key) closes it; nothing is written.
    for (const auto &sequence : {QKeySequence(Qt::Key_Escape), QKeySequence(Qt::CTRL | Qt::Key_W)})
        connect(new QShortcut(sequence, window), &QShortcut::activated, window, &QWidget::close);
    window->setMinimumWidth(320);
    m_infoWindows.insert(path, window);
    window->show();
}
void FinderWindow::showContextMenu(QAbstractItemView *view, QPoint point)
{
    if (m_busy)
        return;
    const auto index = view->indexAt(point).siblingAtColumn(0);
    if (!index.isValid())
        view->clearSelection();
    else if (!view->selectionModel()->isSelected(index))
        view->selectionModel()->select(index, QItemSelectionModel::ClearAndSelect |
                                                  QItemSelectionModel::Rows);
    if (index.isValid())
        view->selectionModel()->setCurrentIndex(index, QItemSelectionModel::NoUpdate);
    updateActions();
    auto *menu = new QMenu(this);
    menu->setObjectName("finderContextMenu");
    menu->setAttribute(Qt::WA_DeleteOnClose);
    const auto add = [this, menu](const char *name)
    {
        if (auto *action = findChild<QAction *>(name))
            menu->addAction(action);
    };
    if (index.isValid())
    {
        add("open");
        add("openWith");
        menu->addSeparator();
        add("getInfo");
        menu->addSeparator();
        add("rename");
        add("duplicate");
        menu->addSeparator();
        add(m_directory == m_operations.trashBase() + "/files" ? "restore" : "trash");
    }
    else
    {
        add("newFolder");
        menu->addSeparator();
        add("selectAll");
        add("cleanUp");
        auto *views = menu->addMenu("View");
        views->addAction(findChild<QAction *>("icons"));
        views->addAction(findChild<QAction *>("list"));
    }
    menu->popup(view->viewport()->mapToGlobal(point));
}
QString FinderWindow::dropDirectoryAt(QPoint windowPosition) const
{
    if (!isVisible() || m_busy || !m_directoryReady || QApplication::activeModalWidget())
        return {};
    auto *view = m_views->currentIndex() == 0 ? static_cast<QAbstractItemView *>(m_icons)
                                              : static_cast<QAbstractItemView *>(m_list);
    const auto local = view->viewport()->mapFrom(this, windowPosition);
    if (!view->viewport()->rect().contains(local))
        return {};
    const auto directory = dropDirectory(view, local);
    return m_operations.readableDirectory(directory) ? directory : QString();
}
QVariantMap FinderWindow::directoryState() const
{
    // Read-only geometry for live qualification. It reports actual viewport
    // rectangles; it does not expose an input-injection or file-command API.
    auto *view = m_views->currentIndex() == 0 ? static_cast<QAbstractItemView *>(m_icons)
                                              : static_cast<QAbstractItemView *>(m_list);
    QVariantList entries;
    for (int row = 0; row < m_model->rowCount(view->rootIndex()) && row < 4096; ++row)
    {
        const auto index = m_model->index(row, 0, view->rootIndex());
        const auto rect = view->visualRect(index);
        const auto point = view->viewport()->mapTo(this, rect.topLeft());
        if (!rect.isValid())
            continue;
        entries.push_back(QVariantMap{{"name", m_model->fileName(index)},
                                      {"x", point.x()},
                                      {"y", point.y()},
                                      {"width", rect.width()},
                                      {"height", rect.height()}});
    }
    const auto contextMenus = findChildren<QMenu *>("finderContextMenu");
    return {{"surfaceId", finderSurfaceId(windowHandle())},
            {"loaded", m_directoryReady},
            {"busy", m_busy},
            {"windowActive", QApplication::activeWindow() == this},
            {"selectedItems", selectedPaths().size()},
            {"listView", m_views->currentIndex() == 1},
            {"showHidden", bool(m_model->filter() & QDir::Hidden)},
            {"entries", entries},
            {"contextMenuVisible", std::any_of(contextMenus.cbegin(), contextMenus.cend(),
                                               [](auto *menu) { return menu->isVisible(); })},
            {"items", m_model->rowCount(m_icons->rootIndex())}};
}
void FinderWindow::setShowHidden(bool show)
{
    QSettings settings(m_profile + "/finder.ini", QSettings::IniFormat);
    settings.setValue("showHiddenFiles", show);
    settings.sync();
    for (auto *widget : QApplication::topLevelWidgets())
        if (auto *window = qobject_cast<FinderWindow *>(widget))
        {
            const QDir::Filters base = QDir::AllEntries | QDir::NoDotAndDotDot;
            window->m_model->setFilter(show ? base | QDir::Hidden : base);
            if (auto *item = window->findChild<QAction *>("showHidden"))
                item->setChecked(show);
        }
}
void FinderWindow::openIndex(const QModelIndex &index)
{
    const QString path = m_model->filePath(index);
    QFileInfo info(path);
    if (info.isDir())
    {
        if (m_operations.readableDirectory(path))
            emit openFolder(path);
        else
            QMessageBox::warning(this, "Open Folder",
                                 "This folder is outside the selected workspace.");
        return;
    }
    if (!m_operations.readableFile(path))
    {
        QMessageBox::warning(this, "Open Document",
                             "This document is unavailable or outside the selected workspace.");
        return;
    }
    emit openDocument(path, false);
}
void FinderWindow::perform(QString label, std::function<FileResult()> job, std::shared_ptr<CopyProgress> bytes,
                           std::shared_ptr<std::atomic_bool> cancel)
{
    if (m_busy)
    {
        QApplication::beep();
        return;
    }
    m_busy = true;
    updateActions();
    auto *progress = new QProgressDialog(label, bytes ? "Stop" : QString(), 0, bytes ? 1000 : 0, this);
    progress->setObjectName("operationProgress");
    progress->setWindowModality(Qt::WindowModal);
    progress->setMinimumDuration(150);
    progress->setAutoReset(false);
    progress->setAutoClose(false);
    if (bytes)
    {
        // Mac OS 9's copy dialog: what remains, and Stop.
        auto *timer = new QTimer(progress);
        connect(timer, &QTimer::timeout, progress, [progress, bytes, label]
        {
            const auto total = std::max<qint64>(1, bytes->total.load());
            const auto done = std::min(bytes->done.load(), total);
            progress->setValue(int(done * 1000 / total));
            progress->setLabelText(label + "\n" + QLocale().formattedDataSize(done) + " of " +
                                   QLocale().formattedDataSize(bytes->total.load()));
        });
        timer->start(150);
        connect(progress, &QProgressDialog::canceled, progress, [progress, cancel]
        {
            if (cancel)
                cancel->store(true);
            progress->setLabelText("Stopping…");
        });
    }
    auto *watcher = new QFutureWatcher<FileResult>(this);
    connect(
        watcher, &QFutureWatcher<FileResult>::finished, this,
        [this, progress, watcher]
        {
            auto result = watcher->result();
            m_busy = false;
            updateActions();
            progress->close();
            progress->deleteLater();
            watcher->deleteLater();
            if (!result.ok)
                QMessageBox::warning(this, "The operation could not be completed", result.error);
        });
    watcher->setFuture(QtConcurrent::run(std::move(job)));
}
void FinderWindow::setViewMode(bool list)
{
    m_views->setCurrentIndex(list ? 1 : 0);
    updateActions();
}
void FinderWindow::saveState()
{
    m_settings->setValue("geometry", saveGeometry());
    m_settings->setValue("list", m_views->currentIndex() == 1);
    if (m_views->currentIndex() == 0)
    {
        QVariantMap positions;
        for (int row = 0; row < m_model->rowCount(m_icons->rootIndex()); ++row)
        {
            auto index = m_model->index(row, 0, m_icons->rootIndex());
            positions.insert(m_model->fileName(index),
                             m_icons->visualRect(index).topLeft() +
                                 QPoint(m_icons->horizontalScrollBar()->value(),
                                        m_icons->verticalScrollBar()->value()));
        }
        m_settings->setValue("positions", positions);
    }
    m_settings->sync();
}
void FinderWindow::restorePositions()
{
    const auto positions = m_settings->value("positions").toMap();
    for (int row = 0; row < m_model->rowCount(m_icons->rootIndex()); ++row)
    {
        auto index = m_model->index(row, 0, m_icons->rootIndex());
        auto key = m_model->fileName(index);
        if (positions.contains(key))
            m_icons->restorePosition(index, positions.value(key).toPoint());
    }
}
void FinderWindow::closeEvent(QCloseEvent *e)
{
    if (m_busy)
    {
        QMessageBox::information(
            this, "File Operation",
            "Wait for the file operation to finish before closing this window.");
        e->ignore();
        return;
    }
    saveState();
    QMainWindow::closeEvent(e);
}

void FinderWindow::setPowerAvailable(bool available)
{
    for (auto *item : m_powerActions)
        item->setVisible(available);
}
void FinderWindow::findItems(const QString &text)
{
    auto *window = new QWidget(nullptr, Qt::Window);
    window->setAttribute(Qt::WA_DeleteOnClose);
    window->setObjectName("findWindow");
    window->setWindowTitle("Find: " + text);
    window->resize(560, 360);
    auto *layout = new QVBoxLayout(window);
    auto *status = new QLabel("Searching “" + QFileInfo(m_directory).fileName() + "”…");
    status->setObjectName("findStatus");
    auto *results = new QTreeWidget;
    results->setObjectName("findResults");
    results->setHeaderLabels({"Name", "Where"});
    results->setRootIsDecorated(false);
    results->setColumnWidth(0, 220);
    auto *enclosing = new QPushButton("Open Enclosing Folder");
    enclosing->setEnabled(false);
    layout->addWidget(status);
    layout->addWidget(results, 1);
    layout->addWidget(enclosing, 0, Qt::AlignRight);
    const auto ops = m_operations;
    const QPointer<FinderWindow> self(this);
    const auto open = [self, ops](const QString &path)
    {
        if (!self)
            return;
        if (QFileInfo(path).isDir() && ops.readableDirectory(path))
            emit self->openFolder(path);
        else if (ops.readableFile(path))
            emit self->openDocument(path, false);
    };
    connect(results, &QTreeWidget::itemDoubleClicked, window,
            [open](QTreeWidgetItem *item) { open(item->data(0, Qt::UserRole).toString()); });
    connect(results, &QTreeWidget::itemSelectionChanged, window,
            [results, enclosing] { enclosing->setEnabled(results->currentItem()); });
    connect(enclosing, &QPushButton::clicked, window,
            [self, results, ops]
            {
                if (self && results->currentItem())
                    if (const auto folder = QFileInfo(results->currentItem()->data(0, Qt::UserRole).toString()).absolutePath();
                        ops.readableDirectory(folder))
                        emit self->openFolder(folder);
            });
    window->show();
    // Names only, never contents; hidden folders and links are not entered; bounded work.
    const QPointer<QWidget> target(window);
    auto *worker = QThread::create(
        [target, status, results, text, root = m_directory, ops]
        {
            QStringList found;
            QDirIterator walk(root, QDir::AllEntries | QDir::NoDotAndDotDot | QDir::System, QDirIterator::Subdirectories);
            qsizetype visited = 0;
            bool complete = true;
            while (walk.hasNext())
            {
                if (++visited > 200000 || found.size() >= 1000 || !target)
                {
                    complete = false;
                    break;
                }
                const auto path = walk.next();
                if (walk.fileName().contains(text, Qt::CaseInsensitive) && ops.inside(QFileInfo(path).canonicalFilePath()))
                    found << path;
            }
            QMetaObject::invokeMethod(qApp, [target, status, results, found, complete, root]
            {
                if (!target)
                    return;
                const QFileIconProvider icons;
                for (const auto &path : found)
                {
                    auto *item = new QTreeWidgetItem(results, {QFileInfo(path).fileName(),
                                                               QDir(root).relativeFilePath(QFileInfo(path).absolutePath())});
                    item->setData(0, Qt::UserRole, path);
                    item->setIcon(0, icons.icon(QFileInfo(path)));
                }
                status->setText(QString("%1 item%2 found%3.").arg(found.size()).arg(found.size() == 1 ? "" : "s")
                                    .arg(complete ? "" : " (the search stopped early; look in a smaller folder)"));
            });
        });
    connect(worker, &QThread::finished, worker, &QObject::deleteLater);
    worker->start();
}
