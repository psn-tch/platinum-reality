#include <QPlainTextEdit>
#include <QClipboard>
#include "appearance/interface_font.hpp"
#include "editor/editor_window.hpp"
#include "finder/file_drag.hpp"
#include "finder/file_operations.hpp"
#include "finder/finder_service.hpp"
#include "finder/finder_window.hpp"
#include "finder/platinum_style.hpp"
#include "finder/window_surface_id.hpp"
#include <QAbstractButton>
#include <QAction>
#include <QApplication>
#include <QContextMenuEvent>
#include <QDialogButtonBox>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QFileDialog>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMenuBar>
#include <QMimeData>
#include <QTreeWidget>
#include <QLabel>
#include <QScopeGuard>
#include <QSignalSpy>
#include <QMessageBox>
#include <QPointer>
#include <QProgressDialog>
#include <QPushButton>
#include <QTemporaryDir>
#include <QTextEdit>
#include <QTimer>
#include <QtTest>

class GuiTests : public QObject
{
    Q_OBJECT
  private slots:
    void cutCopyAndPasteFilesBetweenWindowsAndFileManagers()
    {
        QTemporaryDir root, profile;
        QVERIFY(QDir(root.path()).mkpath("src") && QDir(root.path()).mkpath("dst"));
        for (const auto *name : {"A.txt", "B.txt", "C.txt"})
        {
            QFile f(root.path() + "/src/" + name);
            QVERIFY(f.open(QIODevice::WriteOnly) && f.write(name) > 0);
        }
        QPointer<FinderWindow> source = new FinderWindow(root.path(), profile.path(), root.path() + "/src");
        QPointer<FinderWindow> target = new FinderWindow(root.path(), profile.path(), root.path() + "/dst");
        const auto close = qScopeGuard([&] { if (source) source->close(); if (target) target->close(); });
        source->show();
        target->show();
        QTRY_VERIFY(source->directoryState().value("loaded").toBool() && target->directoryState().value("loaded").toBool());
        auto *icons = source->findChild<FolderIcons *>();
        auto *model = qobject_cast<QFileSystemModel *>(icons->model());
        const auto select = [&](const QString &name)
        {
            icons->selectionModel()->clearSelection();
            icons->selectionModel()->select(model->index(root.path() + "/src/" + name), QItemSelectionModel::Select);
        };
        const auto trigger = [](FinderWindow *window, const char *name)
        {
            auto *action = window->findChild<QAction *>(name);
            QVERIFY(action);
            QTRY_VERIFY(action->isEnabled());
            action->trigger();
        };
        // Copy: names as text (Mac OS 9) and the item itself for any file manager.
        QTRY_VERIFY(model->index(root.path() + "/src/A.txt").isValid());
        select("A.txt");
        trigger(source, "copy");
        const auto *mime = QGuiApplication::clipboard()->mimeData();
        QCOMPARE(mime->text(), QString("A.txt"));
        QCOMPARE(mime->urls(), QList<QUrl>({QUrl::fromLocalFile(root.path() + "/src/A.txt")}));
        QVERIFY(mime->data("x-special/gnome-copied-files").startsWith("copy\n"));
        trigger(target, "paste");
        QTRY_VERIFY(QFileInfo::exists(root.path() + "/dst/A.txt"));
        QVERIFY(QFileInfo::exists(root.path() + "/src/A.txt"));
        // Cut then Paste moves, and the clipboard no longer offers the moved item.
        select("B.txt");
        trigger(source, "cut");
        trigger(target, "paste");
        QTRY_VERIFY(QFileInfo::exists(root.path() + "/dst/B.txt"));
        QTRY_VERIFY(!QFileInfo::exists(root.path() + "/src/B.txt"));
        QTRY_VERIFY(!QGuiApplication::clipboard()->mimeData() || !QGuiApplication::clipboard()->mimeData()->hasUrls());
        // Another file manager's Cut (KDE's marker) pastes as a move too.
        auto *foreign = new QMimeData;
        foreign->setUrls({QUrl::fromLocalFile(root.path() + "/src/C.txt")});
        foreign->setData("application/x-kde-cutselection", "1");
        QGuiApplication::clipboard()->setMimeData(foreign);
        trigger(target, "paste");
        QTRY_VERIFY(QFileInfo::exists(root.path() + "/dst/C.txt") && !QFileInfo::exists(root.path() + "/src/C.txt"));
        // The move is visible before its worker reports back; closing a busy window asks the user to wait.
        QTRY_VERIFY(!target->directoryState().value("busy").toBool());
    }
    void findListsMatchingNamesButNotHiddenFoldersOrLinkedOnes()
    {
        QTemporaryDir root, profile, outside;
        for (const auto *folder : {"A", "B", ".hidden"})
            QVERIFY(QDir(root.path()).mkdir(folder));
        for (const auto *file : {"A/Report.txt", "B/annual report.pdf", ".hidden/report.txt", "B/notes.txt"})
        {
            QFile f(root.path() + "/" + file);
            QVERIFY(f.open(QIODevice::WriteOnly));
        }
        {
            QFile f(outside.path() + "/report outside.txt");
            QVERIFY(f.open(QIODevice::WriteOnly));
        }
        QVERIFY(QFile::link(outside.path(), root.path() + "/linked"));
        QPointer<FinderWindow> window = new FinderWindow(root.path(), profile.path(), root.path());
        const auto close = qScopeGuard([&] { if (window) window->close(); });
        window->findItems("report");
        QWidget *results = nullptr;
        QTRY_VERIFY((results = [] { for (auto *w : QApplication::topLevelWidgets()) if (w->objectName() == "findWindow") return w; return (QWidget *)nullptr; }()));
        const auto closeResults = qScopeGuard([&] { results->close(); });
        auto *list = results->findChild<QTreeWidget *>("findResults");
        QTRY_VERIFY(results->findChild<QLabel *>("findStatus")->text().contains("found"));
        QStringList names;
        for (int i = 0; i < list->topLevelItemCount(); ++i)
            names << list->topLevelItem(i)->text(0);
        names.sort();
        QCOMPARE(names, QStringList({"Report.txt", "annual report.pdf"}));
        QSignalSpy documents(window.data(), &FinderWindow::openDocument);
        emit list->itemDoubleClicked(list->topLevelItem(0), 0);
        QCOMPARE(documents.size(), 1);
    }
    void specialMenuEndsTheSessionOnlyInsideTheDesktop()
    {
        QTemporaryDir root, profile;
        const auto names = [](FinderWindow *window)
        {
            QStringList result;
            for (auto *menu : window->menuBar()->findChildren<QMenu *>())
                if (menu->title() == "Special")
                    for (auto *item : menu->actions())
                        if (item->isVisible())
                            result << (item->isSeparator() ? "-" : item->objectName());
            return result;
        };
        {
            // A Finder on its own cannot end anyone's session.
            FinderWindow alone(root.path(), profile.path(), root.path());
            QCOMPARE(names(&alone), QStringList({"emptyTrash"}));
        }
        qputenv("FRANKENSTEIN_ISOLATED", "1");
        const auto restore = qScopeGuard([] { qunsetenv("FRANKENSTEIN_ISOLATED"); });
        FinderWindow window(root.path(), profile.path(), root.path());
        QCOMPARE(names(&window), QStringList({"emptyTrash", "-", "logOut"}));
        window.setPowerAvailable(true);
        QCOMPARE(names(&window), QStringList({"emptyTrash", "-", "sleep", "restart", "shutDown", "-", "logOut"}));
        QSignalSpy requests(&window, &FinderWindow::sessionRequested);
        for (const auto *name : {"sleep", "restart", "shutDown", "logOut"})
            window.findChild<QAction *>(name)->trigger();
        QCOMPARE(requests.count(), 4);
        QStringList commands;
        for (const auto &request : requests)
            commands << request.at(0).toString();
        QCOMPARE(commands, QStringList({"sleep", "restart", "shut-down", "log-out"}));
    }
    void finderIconsShowPictureThumbnails()
    {
        // A picture shows itself (a framed thumbnail); a folder keeps the Platinum folder.
        QTemporaryDir root;
        QImage picture(120, 60, QImage::Format_RGB32);
        picture.fill(QColor(220, 20, 30));
        QVERIFY(picture.save(root.path() + "/Red Card.png"));
        QVERIFY(QDir(root.path()).mkdir("Folder"));
        QFileSystemModel model;
        installPlatinumIcons(&model);
        model.setRootPath(root.path());
        const auto image = [&](const QString &name)
        { return model.fileIcon(model.index(root.path() + "/" + name)).pixmap(64, 64).toImage(); };
        QTRY_COMPARE(image("Red Card.png").pixelColor(32, 32), QColor(220, 20, 30));
        QVERIFY(image("Folder").pixelColor(32, 32) != QColor(220, 20, 30));
    }

    void goMenuOpensStandardFoldersAndMountedDrives()
    {
        // Go (native adaptation): Macintosh HD's Desktop, and each mounted drive, open in their own
        // windows; drives come and go as they are mounted; nothing outside the places appears.
        QTemporaryDir root, profile, drives;
        QVERIFY(QDir(root.path()).mkdir("Desktop") && QDir(drives.path()).mkdir("USB STICK"));
        FinderPlaces::volumes = {drives.path()};
        const auto reset = qScopeGuard([] { FinderPlaces::volumes.clear(); });
        QPointer<FinderWindow> window = new FinderWindow(root.path(), profile.path(), root.path());
        const auto close = qScopeGuard([&] { if (window) window->close(); });
        window->show();
        QVERIFY(QTest::qWaitForWindowExposed(window));
        QSignalSpy opened(window.data(), &FinderWindow::openFolder);
        auto *desktop = window->findChild<QAction *>("goDesktop");
        QVERIFY(desktop && window->findChild<QAction *>("goHome") && window->findChild<QAction *>("goToFolder"));
        desktop->trigger();
        QCOMPARE(opened.size(), 1);
        QCOMPARE(opened.last().first().toString(), root.path() + "/Desktop");
        QMenu *go = nullptr;
        for (auto *menu : window->menuBar()->findChildren<QMenu *>())
            if (menu->title() == "Go")
                go = menu;
        QVERIFY(go);
        emit go->aboutToShow();
        QList<QAction *> found;
        for (auto *entry : go->actions())
            if (entry->objectName() == "goDrive")
                found << entry;
        QCOMPARE(found.size(), 1);
        QCOMPARE(found.first()->text(), QString("USB STICK"));
        found.first()->trigger();
        QCOMPARE(opened.last().first().toString(), drives.path() + "/USB STICK");
        // Unmounted: gone the next time the menu opens.
        QVERIFY(QDir(drives.path() + "/USB STICK").removeRecursively());
        emit go->aboutToShow();
        for (auto *entry : go->actions())
            QVERIFY(entry->objectName() != "goDrive");
    }

    void finderWindowMenuListsFoldersAndEmptyTrashConfirms()
    {
        // docs/historical-fidelity-review.md F07: Window lists open Finder windows;
        // Special > Empty Trash… defaults to Cancel; Finder offers no Quit.
        QTemporaryDir root, profile;
        QVERIFY(QDir(root.path()).mkpath("Alpha") && QDir(root.path()).mkpath("Beta"));
        FinderService finder(root.path(), profile.path());
        QVERIFY(finder.OpenDirectory(root.path() + "/Alpha"));
        QVERIFY(finder.OpenDirectory(root.path() + "/Beta"));
        FinderWindow *alpha = nullptr;
        for (auto *widget : QApplication::topLevelWidgets())
            if (auto *window = qobject_cast<FinderWindow *>(widget);
                window && window->directory().endsWith("/Alpha"))
                alpha = window;
        QVERIFY(alpha);
        QVERIFY(!alpha->findChild<QAction *>("quit"));
        QMenu *windowMenu = nullptr;
        for (auto *menu : alpha->menuBar()->findChildren<QMenu *>())
            if (menu->title() == "Window")
                windowMenu = menu;
        QVERIFY(windowMenu);
        emit windowMenu->aboutToShow();
        emit windowMenu->aboutToShow(); // Rebuilt, not appended twice.
        QStringList listed;
        QAction *checked = nullptr;
        for (auto *action : windowMenu->actions())
            if (action->objectName().startsWith("window:"))
            {
                listed << QFileInfo(action->objectName().mid(7)).fileName();
                if (action->isChecked())
                    checked = action;
            }
        QCOMPARE(listed, (QStringList{"Alpha", "Beta"}));
        QVERIFY(checked && checked->objectName().endsWith("/Alpha"));
        // Empty Trash: Cancel by default, then OK removes the trashed folder.
        FileOperations operations(root.path(), profile.path() + "/data/Trash");
        QVERIFY(QDir(root.path()).mkpath("Old"));
        QVERIFY(operations.trash(root.path() + "/Old").ok);
        auto *empty = alpha->findChild<QAction *>("emptyTrash");
        QVERIFY(empty);
        bool cancelWasDefault = false;
        const auto answer = [&](bool accept)
        {
            // Poll until the modal question exists; a fixed delay can fire before
            // exec() shows it and leave the test blocked forever.
            auto *poll = new QTimer(this);
            poll->setInterval(20);
            connect(poll, &QTimer::timeout, poll, [poll, accept, &cancelWasDefault]
            {
                for (auto *widget : QApplication::topLevelWidgets())
                    if (auto *box = qobject_cast<QMessageBox *>(widget); box && box->isVisible())
                    {
                        poll->stop();
                        poll->deleteLater();
                        cancelWasDefault = box->defaultButton() == box->button(QMessageBox::Cancel);
                        (accept ? box->button(QMessageBox::Ok) : box->defaultButton())->click();
                        return;
                    }
            });
            QTimer::singleShot(10000, poll, [poll] { poll->stop(); poll->deleteLater(); });
            poll->start();
        };
        answer(false);
        empty->trigger();
        QVERIFY(cancelWasDefault);
        QCOMPARE(operations.trashedItems().size(), 1);
        answer(true);
        empty->trigger();
        QTRY_VERIFY(operations.trashedItems().isEmpty());
        // The worker's completion clears the busy state; closing earlier is refused.
        QTRY_VERIFY(alpha->findChildren<QProgressDialog *>().isEmpty());
        for (auto *widget : QApplication::topLevelWidgets())
            if (qobject_cast<FinderWindow *>(widget))
                widget->close();
    }
    void initTestCase()
    {
        QApplication::setStyle(new PlatinumStyle);
        QApplication::setFont(classicInterfaceFont());
    }
    void folderBackgroundDragSelectsMultipleFiles_data()
    {
        QTest::addColumn<bool>("list");
        QTest::newRow("icons") << false;
        QTest::newRow("list") << true;
    }
    void folderBackgroundDragSelectsMultipleFiles()
    {
        QFETCH(bool, list);
        QTemporaryDir root, profile;
        for (const auto &name : {"One.txt", "Two.txt"})
        {
            QFile file(root.path() + '/' + name);
            QVERIFY(file.open(QIODevice::WriteOnly));
            file.write(name);
        }
        QPointer<FinderWindow> window = new FinderWindow(root.path(), profile.path(), root.path());
        window->show();
        QVERIFY(QTest::qWaitForWindowExposed(window));
        QTRY_VERIFY(window->directoryState().value("loaded").toBool());
        window->setViewMode(list);
        auto *view = list ? static_cast<QAbstractItemView *>(window->findChild<FolderList *>())
                          : static_cast<QAbstractItemView *>(window->findChild<FolderIcons *>());
        auto *model = qobject_cast<QFileSystemModel *>(view->model());
        const auto one = model->index(root.path() + "/One.txt");
        const auto two = model->index(root.path() + "/Two.txt");
        QTRY_VERIFY(view->visualRect(one).isValid() && view->visualRect(two).isValid());
        const auto bounds = view->visualRect(one).united(view->visualRect(two));
        const QPoint start(std::min(view->viewport()->width() - 5, bounds.right() + 20),
                           bounds.bottom() + 30);
        QVERIFY(!view->indexAt(start).isValid());
        QTest::mousePress(view->viewport(), Qt::LeftButton, {}, start);
        QTest::mouseMove(view->viewport(), bounds.topLeft() + QPoint(2, 2), 40);
        QTest::mouseRelease(view->viewport(), Qt::LeftButton, {}, bounds.topLeft() + QPoint(2, 2));
        QSet<QString> selected;
        for (const auto &index : view->selectionModel()->selectedIndexes())
            selected.insert(model->filePath(index.siblingAtColumn(0)));
        QCOMPARE(selected, (QSet<QString>{root.path() + "/One.txt", root.path() + "/Two.txt"}));
        if (!list)
            QVERIFY(static_cast<FolderIcons *>(view)->isSelectionRectVisible());
        QVERIFY(window->grab().save(QCoreApplication::applicationDirPath() +
                                    "/artifacts/finder-marquee-" + (list ? "list" : "icons") +
                                    ".png"));
        window->close();
        QTRY_VERIFY(window.isNull());
    }
    void folderDropTargetsUseTheVisibleViewportAndUpdateTheItemCount()
    {
        QTemporaryDir root, profile;
        QVERIFY(QDir(root.path()).mkdir("Inside"));
        QPointer<FinderWindow> window = new FinderWindow(root.path(), profile.path(), root.path());
        window->show();
        QVERIFY(QTest::qWaitForWindowExposed(window));
        QTRY_VERIFY(window->directoryState().value("loaded").toBool());
        QCOMPARE(finderSurfaceId(nullptr), quint32(0));
        QCOMPARE(finderSurfaceId(window->windowHandle()), quint32(0)); // Offscreen is unsupported.
        auto *count = window->findChild<QLabel *>("folderItemCount");
        QVERIFY(count);
        QTRY_VERIFY2(count->text().startsWith("1 item, "), qPrintable(count->text()));
        for (bool list : {false, true})
        {
            window->setViewMode(list);
            auto *view = list
                             ? static_cast<QAbstractItemView *>(window->findChild<FolderList *>())
                             : static_cast<QAbstractItemView *>(window->findChild<FolderIcons *>());
            auto *model = qobject_cast<QFileSystemModel *>(view->model());
            QVERIFY(model);
            const auto child = model->index(root.path() + "/Inside");
            QTRY_VERIFY(view->visualRect(child).isValid());
            const auto childPoint =
                view->viewport()->mapTo(window, view->visualRect(child).center());
            QCOMPARE(window->dropDirectoryAt(childPoint), root.path() + "/Inside");
            const auto empty =
                view->viewport()->mapTo(window, QPoint(20, view->viewport()->height() - 20));
            QCOMPARE(window->dropDirectoryAt(empty), root.path());
            QVERIFY(window->dropDirectoryAt({-10, -10}).isEmpty());
            QVERIFY(
                window->dropDirectoryAt(count->mapTo(window, count->rect().center())).isEmpty());
        }
        QFile added(root.path() + "/New.txt");
        QVERIFY(added.open(QIODevice::WriteOnly));
        added.write("new");
        added.close();
        QTRY_VERIFY(count->text().startsWith("2 items, "));
        QVERIFY(count->text().endsWith(" available"));
        QCOMPARE(count->alignment(), Qt::Alignment(Qt::AlignCenter));
        QVERIFY(QFile::remove(added.fileName()));
        QTRY_VERIFY2(count->text().startsWith("1 item, "), qPrintable(count->text()));
        window->close();
        QTRY_VERIFY(window.isNull());
    }
    void ownedAlertsKeepButtonMeaningWithoutHostThemeIcons()
    {
        QMessageBox dialog(QMessageBox::Question, "Save Changes?", "Save changes before closing?",
                           QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel);
        dialog.setDefaultButton(QMessageBox::Save);
        dialog.show();
        QVERIFY(QTest::qWaitForWindowExposed(&dialog));
        for (auto *button : dialog.buttons())
            QVERIFY(button->icon().isNull());
        QDir().mkpath(QCoreApplication::applicationDirPath() + "/artifacts");
        QVERIFY(dialog.grab().save(QCoreApplication::applicationDirPath() +
                                   "/artifacts/owned-alert.png"));
        QTest::keyClick(&dialog, Qt::Key_Escape);
        QCOMPARE(dialog.result(), int(QMessageBox::Cancel));
        dialog.show();
        QVERIFY(QTest::qWaitForWindowExposed(&dialog));
        QTest::keyClick(&dialog, Qt::Key_Return);
        QCOMPARE(dialog.result(), int(QMessageBox::Save));
    }
    void editMenuCopiesItemNamesAndShowsTheClipboardLikeMacOS9()
    {
        // As checked in the 9.2.2 Finder: Copy with items selected copies their names as text,
        // one per line in display order; Undo, Cut, Paste and Clear stay disabled; Show Clipboard
        // opens a "Clipboard" window describing its contents. Nothing on disk changes.
        QTemporaryDir root, profile;
        for (const auto *name : {"Alpha.txt", "Beta.txt", "Gamma.txt"})
        {
            QFile file(root.path() + "/" + name);
            QVERIFY(file.open(QIODevice::WriteOnly));
        }
        const auto listing = [&] { return QDir(root.path()).entryList(QDir::AllEntries | QDir::NoDotAndDotDot); };
        const auto before = listing();
        QPointer<FinderWindow> window = new FinderWindow(root.path(), profile.path(), root.path());
        const auto cleanup = qScopeGuard([&] { delete window; });
        window->show();
        QVERIFY(QTest::qWaitForWindowExposed(window));
        QTRY_VERIFY(window->directoryState().value("loaded").toBool());
        auto *icons = window->findChild<FolderIcons *>("iconView");
        auto *model = qobject_cast<QFileSystemModel *>(icons->model());
        const auto action = [&](const char *name) { return window->findChild<QAction *>(name); };
        for (const auto *name : {"undo", "cut", "paste", "clear"})
        {
            QVERIFY2(action(name), name);
            QVERIFY2(!action(name)->isEnabled(), name);
        }
        QVERIFY(action("copy") && action("showClipboard"));
        QTRY_VERIFY(!action("copy")->isEnabled()); // Nothing selected yet.
        for (const auto *name : {"Gamma.txt", "Alpha.txt"})
        {
            const auto index = model->index(root.path() + "/" + name);
            QTRY_VERIFY(icons->visualRect(index).isValid());
            icons->selectionModel()->select(index, QItemSelectionModel::Select);
        }
        QTRY_VERIFY(action("copy")->isEnabled());
        action("copy")->trigger();
        // Display order: by icon position, not selection order.
        const auto alpha = icons->visualRect(model->index(root.path() + "/Alpha.txt"));
        const auto gamma = icons->visualRect(model->index(root.path() + "/Gamma.txt"));
        const bool alphaFirst = alpha.y() != gamma.y() ? alpha.y() < gamma.y() : alpha.x() < gamma.x();
        const auto expected = alphaFirst ? QString("Alpha.txt\nGamma.txt") : QString("Gamma.txt\nAlpha.txt");
        QTRY_COMPARE(QGuiApplication::clipboard()->text(), expected);
        action("showClipboard")->trigger();
        const auto clipboardWindow = [] {
            for (auto *widget : QApplication::topLevelWidgets())
                if (widget->objectName() == "clipboardWindow" && widget->isVisible())
                    return widget;
            return static_cast<QWidget *>(nullptr);
        };
        QWidget *clipboard = nullptr;
        QTRY_VERIFY((clipboard = clipboardWindow()));
        QCOMPARE(clipboard->windowTitle(), QString("Clipboard"));
        QCOMPARE(clipboard->findChild<QLabel *>("clipboardDescription")->text(), QString("Clipboard contents: text"));
        QCOMPARE(clipboard->findChild<QPlainTextEdit *>("clipboardText")->toPlainText(), expected);
        // It follows the clipboard while open.
        QGuiApplication::clipboard()->setText("Beta.txt");
        QTRY_COMPARE(clipboard->findChild<QPlainTextEdit *>("clipboardText")->toPlainText(), QString("Beta.txt"));
        clipboard->close();
        QCOMPARE(listing(), before);
    }
    void getInfoOpensOneModelessInfoWindowPerItem()
    {
        // Finishing School FS-G01: the Finder's Get Info matches the desktop's (shared facts):
        // modeless, one window per item, bound to that item, closed by Escape; nothing written.
        QTemporaryDir root, profile;
        {
            QFile notes(root.path() + "/Notes.txt");
            QVERIFY(notes.open(QIODevice::WriteOnly) && notes.write("hello, world") == 12);
        }
        QVERIFY(QDir(root.path()).mkdir("Photos"));
        const auto listing = [&] { return QDir(root.path()).entryList(QDir::AllEntries | QDir::NoDotAndDotDot); };
        const auto before = listing();
        QPointer<FinderWindow> window = new FinderWindow(root.path(), profile.path(), root.path());
        const auto cleanup = qScopeGuard([&] { delete window; });
        window->show();
        QVERIFY(QTest::qWaitForWindowExposed(window));
        QTRY_VERIFY(window->directoryState().value("loaded").toBool());
        auto *icons = window->findChild<FolderIcons *>("iconView");
        auto *model = qobject_cast<QFileSystemModel *>(icons->model());
        const auto select = [&](const QString &name)
        {
            const auto index = model->index(root.path() + "/" + name);
            QTRY_VERIFY(icons->visualRect(index).isValid());
            icons->selectionModel()->select(index, QItemSelectionModel::ClearAndSelect);
        };
        const auto info = [&](const QString &title)
        {
            QList<QWidget *> found;
            for (auto *widget : QApplication::topLevelWidgets())
                if (widget->isVisible() && widget->windowTitle() == title)
                    found << widget;
            return found;
        };
        auto *getInfo = window->findChild<QAction *>("getInfo");
        QVERIFY(getInfo);
        select("Notes.txt");
        getInfo->trigger();
        QTRY_COMPARE(info("Notes.txt Info").size(), 1);
        QPointer<QWidget> notes = info("Notes.txt Info").first();
        QCOMPARE(notes->findChild<QLabel *>("infoName")->text(), QString("Notes.txt"));
        QCOMPARE(notes->findChild<QLabel *>("infoKind")->text(), QString("Plain text document"));
        QCOMPARE(notes->findChild<QLabel *>("infoSize")->text(), QString("12 bytes (12 bytes)"));
        QCOMPARE(notes->findChild<QLabel *>("infoWhere")->text(), root.path());
        QVERIFY(!QApplication::activeModalWidget());
        getInfo->trigger();
        QTest::qWait(50);
        QCOMPARE(info("Notes.txt Info").size(), 1);
        // Bound to its item: another selection opens its own window.
        select("Photos");
        getInfo->trigger();
        QTRY_COMPARE(info("Photos Info").size(), 1);
        QPointer<QWidget> photos = info("Photos Info").first();
        QCOMPARE(photos->findChild<QLabel *>("infoSize")->text(), QString("Contains 0 items"));
        QCOMPARE(notes->findChild<QLabel *>("infoName")->text(), QString("Notes.txt"));
        QVERIFY(notes->grab().save(QCoreApplication::applicationDirPath() + "/artifacts/finder-get-info.png"));
        notes->activateWindow();
        QVERIFY(QTest::qWaitForWindowActive(notes));
        QTest::keyClick(notes, Qt::Key_Escape);
        QTRY_VERIFY(!notes);
        photos->close();
        QTRY_VERIFY(!photos);
        QCOMPARE(listing(), before);
    }
    void folderDoubleClickOpensSpatialWindow()
    {
        QTemporaryDir root;
        QTemporaryDir profile;
        QDir(root.path()).mkdir("Documents");
        auto *w = new FinderWindow(root.path(), profile.path(), root.path());
        w->show();
        QSignalSpy opened(w, &FinderWindow::openFolder);
        auto *view = w->findChild<FolderIcons *>("iconView");
        auto *model = qobject_cast<QFileSystemModel *>(view->model());
        auto index = model->index(root.path() + "/Documents");
        QTRY_VERIFY(view->visualRect(index).isValid());
        QTest::qWait(100);
        const auto point = view->visualRect(index).center();
        QTest::mouseClick(view->viewport(), Qt::LeftButton, {}, point);
        QTest::mouseDClick(view->viewport(), Qt::LeftButton, {}, point);
        QTRY_COMPARE(opened.count(), 1);
        QCOMPARE(opened.first().first().toString(), root.path() + "/Documents");
        w->close();
    }
    void contextMenusSelectTheClickedItemPreserveGroupsAndOfferBackgroundActions()
    {
        QTemporaryDir root, profile;
        for (const auto &name : {"One.txt", "Two.txt"})
        {
            QFile file(root.path() + '/' + name);
            QVERIFY(file.open(QIODevice::WriteOnly));
            file.write(name);
        }
        QPointer<FinderWindow> window = new FinderWindow(root.path(), profile.path(), root.path());
        window->show();
        QVERIFY(QTest::qWaitForWindowExposed(window));
        QTRY_VERIFY(window->directoryState().value("loaded").toBool());
        auto *icons = window->findChild<FolderIcons *>("iconView");
        auto *model = qobject_cast<QFileSystemModel *>(icons->model());
        auto index = model->index(root.path() + "/One.txt");
        QTRY_VERIFY(icons->visualRect(index).isValid());
        const auto context = [&](QAbstractItemView *view, QPoint point)
        {
            QContextMenuEvent event(QContextMenuEvent::Mouse, point,
                                    view->viewport()->mapToGlobal(point));
            QApplication::sendEvent(view->viewport(), &event);
        };
        context(icons, icons->visualRect(index).center());
        QTRY_VERIFY(window->findChild<QMenu *>("finderContextMenu"));
        QPointer<QMenu> menu = window->findChild<QMenu *>("finderContextMenu");
        QVERIFY(menu->isVisible());
        QVERIFY(menu->actions().contains(window->findChild<QAction *>("rename")));
        QVERIFY(window->findChild<QAction *>("duplicate")->isEnabled());
        QVERIFY(menu->grab().save(QCoreApplication::applicationDirPath() +
                                  "/artifacts/finder-context.png"));
        auto *duplicate = window->findChild<QAction *>("duplicate");
        QTest::mouseClick(menu, Qt::LeftButton, {}, menu->actionGeometry(duplicate).center());
        QTRY_VERIFY(QFileInfo::exists(root.path() + "/One copy.txt"));
        QTRY_VERIFY(!window->directoryState().value("busy").toBool());
        QTRY_VERIFY(menu.isNull());
        // "One copy.txt" sorts before "One.txt": look the item up again once the copy is listed and placed.
        QTRY_VERIFY(icons->visualRect(model->index(root.path() + "/One copy.txt")).isValid());
        index = model->index(root.path() + "/One.txt");
        QTRY_VERIFY(icons->visualRect(index).isValid());
        icons->selectionModel()->select(index, QItemSelectionModel::ClearAndSelect);
        icons->selectionModel()->select(model->index(root.path() + "/Two.txt"),
                                        QItemSelectionModel::Select);
        context(icons, icons->visualRect(index).center());
        QTRY_VERIFY(window->findChild<QMenu *>("finderContextMenu"));
        menu = window->findChild<QMenu *>("finderContextMenu");
        QCOMPARE(icons->selectionModel()->selectedIndexes().size(), 2);
        QVERIFY(!window->findChild<QAction *>("rename")->isEnabled());
        QVERIFY(window->findChild<QAction *>("trash")->isEnabled());
        QTest::keyClick(menu, Qt::Key_Escape);
        QTRY_VERIFY(menu.isNull());
        window->setViewMode(true);
        auto *list = window->findChild<FolderList *>("listView");
        QVERIFY(list);
        context(list, list->visualRect(model->index(root.path() + "/Two.txt")).center());
        QTRY_VERIFY(window->findChild<QMenu *>("finderContextMenu"));
        menu = window->findChild<QMenu *>("finderContextMenu");
        QSignalSpy opened(window, &FinderWindow::openDocument);
        auto *open = window->findChild<QAction *>("open");
        QTest::mouseClick(menu, Qt::LeftButton, {}, menu->actionGeometry(open).center());
        QTRY_COMPARE(opened.count(), 1);
        QCOMPARE(opened.first().first().toString(), root.path() + "/Two.txt");
        QTRY_VERIFY(menu.isNull());
        context(list, {list->viewport()->width() - 30, list->viewport()->height() - 30});
        QTRY_VERIFY(window->findChild<QMenu *>("finderContextMenu"));
        menu = window->findChild<QMenu *>("finderContextMenu");
        QVERIFY(menu->actions().contains(window->findChild<QAction *>("newFolder")));
        QVERIFY(!menu->actions().contains(window->findChild<QAction *>("trash")));
        QVERIFY(list->selectionModel()->selectedIndexes().isEmpty());
        QTest::keyClick(menu, Qt::Key_Escape);
        QTRY_VERIFY(menu.isNull());
        window->close();
        QTRY_VERIFY(window.isNull());
    }
    void fileDropsUseTheActualViewAndPreserveCopySources_data()
    {
        QTest::addColumn<bool>("list");
        QTest::addColumn<bool>("copy");
        QTest::newRow("icons-move") << false << false;
        QTest::newRow("icons-copy") << false << true;
        QTest::newRow("list-move") << true << false;
        QTest::newRow("list-copy") << true << true;
    }
    void fileDropsUseTheActualViewAndPreserveCopySources()
    {
        QFETCH(bool, list);
        QFETCH(bool, copy);
        QTemporaryDir root, profile;
        QVERIFY(QDir(root.path()).mkdir("From"));
        QVERIFY(QDir(root.path()).mkdir("To"));
        const auto source = root.path() + "/From/Dragged.txt",
                   target = root.path() + "/To/Dragged.txt";
        {
            QFile file(source);
            QVERIFY(file.open(QIODevice::WriteOnly));
            file.write("The original bytes.\n");
        }
        QPointer<FinderWindow> window =
            new FinderWindow(root.path(), profile.path(), root.path() + "/To");
        window->setViewMode(list);
        window->show();
        QVERIFY(QTest::qWaitForWindowExposed(window));
        auto *view =
            list ? static_cast<QAbstractItemView *>(window->findChild<FolderList *>("listView"))
                 : static_cast<QAbstractItemView *>(window->findChild<FolderIcons *>("iconView"));
        QMimeData mime;
        mime.setUrls({QUrl::fromLocalFile(source)});
        mime.setData(finderDragMime, "1");
        const auto modifiers = copy ? Qt::AltModifier : Qt::NoModifier;
        QDragEnterEvent enter(QPointF(60, 60), Qt::CopyAction | Qt::MoveAction, &mime,
                              Qt::LeftButton, modifiers);
        QApplication::sendEvent(view->viewport(), &enter);
        QVERIFY(enter.isAccepted());
        QDropEvent drop({60, 60}, Qt::CopyAction | Qt::MoveAction, &mime, Qt::LeftButton,
                        modifiers);
        QApplication::sendEvent(view->viewport(), &drop);
        QVERIFY(drop.isAccepted());
        QCOMPARE(drop.dropAction(), copy ? Qt::CopyAction : Qt::MoveAction);
        QTRY_VERIFY(QFileInfo::exists(target));
        QTRY_VERIFY(!window->directoryState().value("busy").toBool());
        QCOMPARE(QFileInfo::exists(source), copy);
        QFile result(target);
        QVERIFY(result.open(QIODevice::ReadOnly));
        QCOMPARE(result.readAll(), QByteArray("The original bytes.\n"));
        window->close();
        QTRY_VERIFY(window.isNull());
    }
    void fileDragPayloadRejectsMixedRemoteRelativeAndOversizedBatches()
    {
        QMimeData mime;
        mime.setUrls({QUrl::fromLocalFile("/tmp/A"), QUrl::fromLocalFile("/tmp/A")});
        QCOMPARE(fileDragPaths(&mime), QStringList{"/tmp/A"});
        mime.setUrls({QUrl::fromLocalFile("/tmp/A"), QUrl("https://example.com/A")});
        QVERIFY(fileDragPaths(&mime).isEmpty());
        mime.setUrls({QUrl::fromLocalFile("relative")});
        QVERIFY(fileDragPaths(&mime).isEmpty());
        QList<QUrl> urls;
        for (int n = 0; n < 129; ++n)
            urls.push_back(QUrl::fromLocalFile("/tmp/" + QString::number(n)));
        mime.setUrls(urls);
        QVERIFY(fileDragPaths(&mime).isEmpty());
    }
    void ownedFileDragPayloadPreservesValidationWithoutPortalFallback_data()
    {
        QTest::addColumn<QByteArray>("payload");
        QTest::addColumn<QStringList>("expected");
        const auto valid = QUrl::fromLocalFile(QString::fromUtf8("/tmp/Café #1.txt")).toEncoded();
        QTest::newRow("encoded names and duplicates")
            << valid + "\r\n" + valid + "\r\n"
            << QStringList{QString::fromUtf8("/tmp/Café #1.txt")};
        QTest::newRow("mixed local and remote") << valid + "\r\nhttps://example.com/file\r\n"
                                                << QStringList{};
        QTest::newRow("decoded NUL") << QByteArray("file:///tmp/a%00b\r\n") << QStringList{};
        QTest::newRow("literal NUL") << QByteArray("file:///tmp/a\0b\r\n", 17) << QStringList{};
        QTest::newRow("decoded size limit") << QByteArray(256 * 1024 + 1, 'a') << QStringList{};
        QByteArray many;
        for (int n = 0; n < 129; ++n)
            many += "file:///tmp/" + QByteArray::number(n) + "\r\n";
        QTest::newRow("URL count limit") << many << QStringList{};
        QTest::newRow("empty owned payload") << QByteArray{} << QStringList{};
    }
    void ownedFileDragPayloadPreservesValidationWithoutPortalFallback()
    {
        QFETCH(QByteArray, payload);
        QFETCH(QStringList, expected);
        QMimeData mime;
        mime.setUrls({QUrl::fromLocalFile("/tmp/Fallback.txt")});
        mime.setData(finderDragMime, "transport-token");
        mime.setData(finderFileUrlsMime, payload);
        QCOMPARE(fileDragPaths(&mime), expected);
        // Without the owned-drag marker, an unrelated custom format is ignored.
        mime.removeFormat(finderDragMime);
        QCOMPARE(fileDragPaths(&mime), QStringList{"/tmp/Fallback.txt"});
    }
    void fileOpeningRequestsTheDesktopAndDesktopEntriesStayDocuments()
    {
        QTemporaryDir root, profile;
        const auto path = root.path() + "/An application.desktop";
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write("[Desktop Entry]\nType=Application\nName=Inert\nExec=/usr/bin/false\n");
        file.close();
        auto *window = new FinderWindow(root.path(), profile.path(), root.path());
        QSignalSpy opened(window, &FinderWindow::openDocument);
        window->show();
        QVERIFY(QTest::qWaitForWindowExposed(window));
        QTRY_VERIFY(window->directoryState().value("loaded").toBool());
        auto *icons = window->findChild<FolderIcons *>("iconView");
        QVERIFY(icons);
        auto *model = qobject_cast<QFileSystemModel *>(icons->model());
        QVERIFY(model);
        icons->selectionModel()->select(model->index(path), QItemSelectionModel::Select);
        QVERIFY(!icons->selectionModel()->selectedIndexes().isEmpty());
        auto *open = window->findChild<QAction *>("open");
        QVERIFY(open);
        open->trigger();
        QCOMPARE(opened.count(), 1);
        QCOMPARE(opened.first().at(0).toString(), path);
        QVERIFY(!opened.first().at(1).toBool());
        auto *with = window->findChild<QAction *>("openWith");
        QVERIFY(with);
        with->trigger();
        QCOMPARE(opened.count(), 2);
        QVERIFY(opened.last().at(1).toBool());
        window->close();
    }
    void editorSaveAsUsesInSessionPickerAndCancelKeepsItsPath()
    {
        QTemporaryDir root;
        EditorWindow window;
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        auto *text = window.findChild<QTextEdit *>("documentText");
        QVERIFY(text);
        text->setPlainText("A new document saved with the in-session picker.");
        auto *saveAs = window.findChild<QAction *>("saveAs");
        QVERIFY(saveAs);
        const auto path = root.path() + "/New document.txt";
        bool seen = false;
        QTimer responder;
        connect(&responder, &QTimer::timeout, &window,
                [&]
                {
                    auto *dialog = qobject_cast<QFileDialog *>(QApplication::activeModalWidget());
                    if (!dialog)
                        return;
                    seen = true;
                    QVERIFY(dialog->testOption(QFileDialog::DontUseNativeDialog));
                    auto *name = dialog->findChild<QLineEdit *>("fileNameEdit");
                    auto *buttons = dialog->findChild<QDialogButtonBox *>();
                    if (!name || !buttons)
                        return;
                    if (name->text() != path)
                    {
                        name->setText(path);
                        return;
                    }
                    auto *save = buttons->button(QDialogButtonBox::Save);
                    if (!save || !save->isEnabled())
                        return;
                    responder.stop();
                    QTest::mouseClick(save, Qt::LeftButton);
                });
        QTimer watchdog;
        watchdog.setSingleShot(true);
        watchdog.setInterval(3000);
        connect(
            &watchdog, &QTimer::timeout, &window,
            []
            {
                if (auto *dialog = qobject_cast<QFileDialog *>(QApplication::activeModalWidget()))
                {
                    qWarning() << "Picker did not finish; selection:" << dialog->selectedFiles();
                    dialog->reject();
                }
            });
        responder.start(10);
        watchdog.start();
        saveAs->trigger();
        responder.stop();
        watchdog.stop();
        QVERIFY(seen);
        QFile saved(path);
        QVERIFY(saved.open(QIODevice::ReadOnly));
        QCOMPARE(saved.readAll(), QByteArray("A new document saved with the in-session picker."));
        QVERIFY(window.windowTitle().contains("New document.txt"));
        const auto title = window.windowTitle();
        QTimer::singleShot(0, &window,
                           []
                           {
                               if (auto *dialog =
                                       qobject_cast<QDialog *>(QApplication::activeModalWidget()))
                                   dialog->reject();
                           });
        saveAs->trigger();
        QCOMPARE(window.windowTitle(), title);
        window.close();
    }
    void unsupportedTextCannotSilentlyReplaceOriginalBytes_data()
    {
        QTest::addColumn<QByteArray>("bytes");
        QTest::newRow("invalid UTF-8") << QByteArray("text\xff", 5);
        QTest::newRow("incomplete UTF-8") << QByteArray("text\xe2\x82", 6);
        QTest::newRow("embedded NUL") << QByteArray("text\0binary", 11);
    }
    void unsupportedTextCannotSilentlyReplaceOriginalBytes()
    {
        QFETCH(QByteArray, bytes);
        QTemporaryDir root;
        const auto path = root.path() + "/Unsupported.txt";
        {
            QFile file(path);
            QVERIFY(file.open(QIODevice::WriteOnly));
            QCOMPARE(file.write(bytes), bytes.size());
        }
        bool alerted = false;
        QTimer::singleShot(0, this,
                           [&]
                           {
                               auto *dialog =
                                   qobject_cast<QMessageBox *>(QApplication::activeModalWidget());
                               QVERIFY(dialog);
                               alerted = true;
                               dialog->accept();
                           });
        EditorWindow window(path);
        QVERIFY(alerted);
        QCOMPARE(window.windowTitle(), QString("untitled"));
        QVERIFY(window.findChild<QTextEdit *>("documentText")->toPlainText().isEmpty());
        QTimer::singleShot(0, &window,
                           []
                           {
                               if (auto *dialog =
                                       qobject_cast<QDialog *>(QApplication::activeModalWidget()))
                                   dialog->reject();
                           });
        QVERIFY(!window.save()); // A rejected load has no writable original path.
        QFile original(path);
        QVERIFY(original.open(QIODevice::ReadOnly));
        QCOMPARE(original.readAll(), bytes);
    }
    void folderViewAndGeometryPersist()
    {
        QTemporaryDir root;
        QTemporaryDir profile;
        QPointer<FinderWindow> w = new FinderWindow(root.path(), profile.path(), root.path());
        w->show();
        w->resize(580, 360);
        w->setViewMode(true);
        const QSize size = w->size();
        w->close();
        QTRY_VERIFY(w.isNull());
        auto *next = new FinderWindow(root.path(), profile.path(), root.path());
        next->show();
        QCOMPARE(next->size(), size);
        QVERIFY(next->findChild<QTreeView *>("listView")->isVisible());
        next->close();
    }
    void hiddenFilesAreShownInEveryWindowAndRemembered()
    {
        // psn_tch, 2026-10-22: "make it so that you can hide and show hidden files".
        QTemporaryDir root;
        QTemporaryDir profile;
        QDir(root.path()).mkdir("Folder");
        for (const auto *name : {"/.hidden", "/visible", "/Folder/.config", "/Folder/notes"})
        {
            QFile file(root.path() + name);
            QVERIFY(file.open(QIODevice::WriteOnly));
        }
        auto *top = new FinderWindow(root.path(), profile.path(), root.path());
        auto *inner = new FinderWindow(root.path(), profile.path(), root.path() + "/Folder");
        top->show();
        inner->show();
        const auto count = [](FinderWindow *w) { return w->directoryState().value("items").toInt(); };
        QTRY_COMPARE(count(top), 2);   // Folder, visible
        QTRY_COMPARE(count(inner), 1); // notes
        auto *show = top->findChild<QAction *>("showHidden");
        QVERIFY(show && show->isCheckable() && !show->isChecked());
        show->trigger();
        QTRY_COMPARE(count(top), 3);
        QTRY_COMPARE(count(inner), 2);
        QVERIFY(inner->findChild<QAction *>("showHidden")->isChecked());
        QVERIFY(inner->directoryState().value("showHidden").toBool());
        // A new window opens the way the Finder was left.
        auto *again = new FinderWindow(root.path(), profile.path(), root.path() + "/Folder");
        again->show();
        QTRY_COMPARE(count(again), 2);
        QVERIFY(again->findChild<QAction *>("showHidden")->isChecked());
        again->findChild<QAction *>("showHidden")->trigger();
        QTRY_COMPARE(count(top), 2);
        QTRY_COMPARE(count(inner), 1);
        QTRY_COMPARE(count(again), 1);
        for (auto *w : {top, inner, again})
            w->close();
    }
    void repeatedOpenUsesOneFolderWindow()
    {
        QTemporaryDir root;
        QTemporaryDir profile;
        QDir(root.path()).mkdir("Documents");
        FinderService finder(root.path(), profile.path());
        QVERIFY(finder.OpenDirectory(root.path() + "/Documents"));
        QVERIFY(finder.OpenDirectory(root.path() + "/Documents/../Documents"));
        QCOMPARE(finder.windowCount(), 1);
        QTRY_VERIFY(finder.DirectoryState(root.path() + "/Documents").value("loaded").toBool());
        QVERIFY(!finder.DirectoryState(root.path() + "/Documents").value("busy").toBool());
        QTemporaryDir outside;
        QVERIFY(!finder.OpenDirectory(outside.path()));
        QVERIFY(finder.DirectoryState(outside.path()).isEmpty());
        QCOMPARE(finder.windowCount(), 1);
        for (auto *window : QApplication::topLevelWidgets())
            if (qobject_cast<FinderWindow *>(window))
                window->close();
        QTRY_COMPARE(finder.windowCount(), 0);
    }
    void newFolderThroughActualDialog()
    {
        QTemporaryDir root;
        QTemporaryDir profile;
        auto *w = new FinderWindow(root.path(), profile.path(), root.path());
        w->show();
        QTimer::singleShot(0,
                           []
                           {
                               auto *dialog =
                                   qobject_cast<QInputDialog *>(QApplication::activeModalWidget());
                               QVERIFY(dialog);
                               dialog->setTextValue("Created by GUI");
                               dialog->accept();
                           });
        w->findChild<QAction *>("newFolder")->trigger();
        QTRY_VERIFY(QFileInfo(root.path() + "/Created by GUI").isDir());
        QTest::qWait(100);
        w->close();
    }
    void editAndSaveActualDocument()
    {
        QTemporaryDir root;
        const auto path = root.path() + "/document.txt";
        QFile f(path);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write("old");
        f.close();
        EditorWindow window(path);
        window.show();
        window.activateWindow();
        QVERIFY(QTest::qWaitForWindowActive(&window));
        auto *text = window.findChild<QTextEdit *>("documentText");
        text->setFocus();
        QTest::keyClick(text, Qt::Key_A, Qt::ControlModifier);
        QTest::keyClicks(text, "A native Macintosh experience.");
        QTest::keyClick(text, Qt::Key_S, Qt::ControlModifier);
        QTRY_VERIFY(!text->document()->isModified());
        QVERIFY(f.open(QIODevice::ReadOnly));
        QCOMPARE(f.readAll(), QByteArray("A native Macintosh experience."));
        f.close();
        window.close();
    }
    void renderOwnedFinder()
    {
        QTemporaryDir root;
        QTemporaryDir profile;
        QDir(root.path()).mkdir("Documents");
        QDir(root.path()).mkdir("Applications");
        QFile file(root.path() + "/Read Me.txt");
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write("Frankenstein Macintosh\nNative files. Native applications.\n");
        file.close();
        auto *window = new FinderWindow(root.path(), profile.path(), root.path());
        window->setWindowTitle("Hard Disk");
        window->show();
        QTest::qWait(250);
        QDir().mkpath(QCoreApplication::applicationDirPath() + "/artifacts");
        auto image = window->grab().toImage();
        QVERIFY(!image.isNull());
        QVERIFY(image.save(QCoreApplication::applicationDirPath() + "/artifacts/finder.png"));
        window->close();
    }
    void dirtyDocumentCanCancelClose()
    {
        EditorWindow window;
        window.show();
        auto *text = window.findChild<QTextEdit *>("documentText");
        text->insertPlainText("Unsaved work");
        QTimer::singleShot(0,
                           []
                           {
                               auto *dialog =
                                   qobject_cast<QMessageBox *>(QApplication::activeModalWidget());
                               QVERIFY(dialog);
                               dialog->button(QMessageBox::Cancel)->click();
                           });
        QVERIFY(!window.close());
        QVERIFY(window.isVisible());
        QCOMPARE(text->toPlainText(), QString("Unsaved work"));
        QTimer::singleShot(0,
                           []
                           {
                               auto *dialog =
                                   qobject_cast<QMessageBox *>(QApplication::activeModalWidget());
                               QVERIFY(dialog);
                               dialog->button(QMessageBox::Discard)->click();
                           });
        QVERIFY(window.close());
    }
    void commandShortcutSavesDocument()
    {
        QTemporaryDir dir;
        const auto path = dir.path() + "/native.txt";
        QFile f(path);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write("before");
        f.close();
        EditorWindow window(path);
        window.show();
        window.activateWindow();
        QVERIFY(QTest::qWaitForWindowActive(&window));
        auto *text = window.findChild<QTextEdit *>("documentText");
        text->selectAll();
        text->insertPlainText("after");
        text->setFocus();
        QTest::keyClick(text, Qt::Key_S, Qt::MetaModifier);
        QTRY_VERIFY(!text->document()->isModified());
        QVERIFY(f.open(QIODevice::ReadOnly));
        QCOMPARE(f.readAll(), QByteArray("after"));
        window.close();
    }
};
QTEST_MAIN(GuiTests)
#include "gui_tests.moc"
