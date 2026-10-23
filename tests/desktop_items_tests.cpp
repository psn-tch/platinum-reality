#include "desktop/desktop_items.hpp"
#include <QScopeGuard>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QRegion>
#include <QTemporaryDir>
#include <QSettings>
#include <QtTest>
#include <unistd.h>

class DesktopItemsTests : public QObject
{
    Q_OBJECT
    static bool write(const QString &path, QByteArray bytes = "document")
    {
        QFile file(path);
        return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size();
    }
    static QModelIndex named(const DesktopItems &items, const QString &name)
    {
        for (int row = 0; row < items.rowCount(); ++row)
            if (items.data(items.index(row), DesktopItems::NameRole).toString() == name)
                return items.index(row);
        return {};
    }
    static QString key(const DesktopItems &items, const QString &name)
    {
        return items.data(named(items, name), DesktopItems::KeyRole).toString();
    }
    static QPoint point(const DesktopItems &items, const QString &name)
    {
        const auto index = named(items, name);
        return {items.data(index, DesktopItems::XRole).toInt(),
                items.data(index, DesktopItems::YRole).toInt()};
    }
  private slots:
    void trustedLaunchersShowTheirNamesAndDrivesAppearAsDisks()
    {
        QTemporaryDir root, profile, drives;
        DesktopItems items(root.path(), profile.path());
        const auto launcher = items.directory() + "/mac-os-9.desktop";
        QVERIFY(write(launcher, "[Desktop Entry]\nType=Application\nName=Mac OS 9.2.2\nExec=/usr/bin/true\n"));
        QVERIFY(QFile::setPermissions(launcher, QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner));
        QVERIFY(write(items.directory() + "/inert.desktop", "[Desktop Entry]\nType=Application\nName=Inert\nExec=/usr/bin/true\n"));
        QVERIFY(QFile::link(launcher, items.directory() + "/linked.desktop"));
        items.refresh();
        // Only the user's own executable entry is a launcher; the others stay files and links.
        QCOMPARE(items.data(named(items, "Mac OS 9.2.2"), DesktopItems::KindRole).toString(), QString("launcher"));
        QCOMPARE(items.data(named(items, "inert.desktop"), DesktopItems::KindRole).toString(), QString("file"));
        QCOMPARE(items.data(named(items, "linked.desktop"), DesktopItems::KindRole).toString(), QString("link"));
        // Renaming offers the file's own name, so the .desktop ending is kept.
        items.select(key(items, "Mac OS 9.2.2"), false);
        QCOMPARE(items.renameTarget().value("name").toString(), QString("mac-os-9.desktop"));
        // Mounted drives are disks; unmounting removes them; a drive cannot be modified.
        QVERIFY(QDir(drives.path()).mkdir("USB STICK"));
        FinderPlaces::volumes = {drives.path()};
        const auto reset = qScopeGuard([] { FinderPlaces::volumes.clear(); });
        DesktopItems withDrives(root.path(), profile.path());
        QCOMPARE(withDrives.data(named(withDrives, "USB STICK"), DesktopItems::KindRole).toString(), QString("disk"));
        withDrives.select(key(withDrives, "USB STICK"), false);
        QVERIFY(!withDrives.canModifySelection());
        QVERIFY(QDir(drives.path() + "/USB STICK").removeRecursively());
        QTRY_VERIFY_WITH_TIMEOUT(!named(withDrives, "USB STICK").isValid(), 8000);
    }
    void monitorGapsAreNotIconSlotsOrFileDropDestinations()
    {
        QTemporaryDir root, profile;
        DesktopItems items(root.path(), profile.path());
        // Negative host origin, different screen heights, and a 100px gap.
        const auto layout = *DisplayLayout::create({{"DP-3", {-620,-160,520,600},1.35,"normal"},
                                                    {"eDP-1", {0,0,400,340},2.3,"270"}},
                                                   "DP-3", DisplayLayout::Mode::Extend);
        items.setDisplayLayout(layout);
        for (int i=0; i<24; ++i)
            QVERIFY(write(items.directory()+QString("/File%1.txt").arg(i), "real file"));
        items.refresh();
        items.cleanUp();
        QRegion visible(QRect(0,40,520,560)); visible += QRect(620,174,400,326);
        QVector<QRect> occupied;
        bool secondary=false;
        for(int row=0;row<items.rowCount();++row)
        {
            const auto at=items.index(row);
            const QPoint p(items.data(at,DesktopItems::XRole).toInt(),items.data(at,DesktopItems::YRole).toInt());
            const QRect rectangle(p,QSize(100,76));
            QVERIFY2(QRegion(rectangle).subtracted(visible).isEmpty(),qPrintable(items.data(at,DesktopItems::NameRole).toString()));
            for(const auto &other:occupied) QVERIFY(!rectangle.intersects(other));
            occupied.append(rectangle);
            secondary=secondary||p.x()>=620;
        }
        QVERIFY(secondary);
        QVERIFY(!items.acceptsPoint(570,230));
        QVERIFY(!items.acceptsPoint(750,100));
        QVERIFY(!items.acceptsPoint(750,550));
        QVERIFY(!items.acceptsPoint(1020,250));
        QVERIFY(!items.acceptsPoint(100,10));
        QVERIFY(items.acceptsPoint(700,180));
        QVERIFY(!items.beginSelection(570,230));
        QVERIFY(items.beginSelection(700,180));
        items.endSelection();
        const auto source=root.path()+"/New.txt";
        QVERIFY(write(source,"do not lose this"));
        QVERIFY(!items.receiveFiles({source},{570,230},false));
        QVERIFY(QFileInfo::exists(source));
        QVERIFY(!QFileInfo::exists(items.directory()+"/New.txt"));
        QVERIFY(!items.busy());
        // A valid drop near the short display's bottom must rescue the full
        // icon footprint and store that landing, not put its label in a gap.
        QVERIFY(items.receiveFiles({source},{800,490},false));
        QTRY_VERIFY(!items.busy());
        QVERIFY(!QFileInfo::exists(source));
        QVERIFY(QRegion(QRect(point(items,"New.txt"),QSize(100,76))).subtracted(visible).isEmpty());
        QFile saved(profile.path()+"/desktop.json"); QVERIFY(saved.open(QIODevice::ReadOnly));
        const auto stored=QJsonDocument::fromJson(saved.readAll()).object()["positions"].toObject()[key(items,"New.txt")].toArray();
        QCOMPARE(QPoint(stored[0].toInt(),stored[1].toInt()),point(items,"New.txt"));
        QFile file(items.directory()+"/New.txt"); QVERIFY(file.open(QIODevice::ReadOnly));
        QCOMPARE(file.readAll(),QByteArray("do not lose this"));
    }
    void disconnectRescueRetainsSavedPositionsAndCancelsGestures()
    {
        QTemporaryDir root, profile;
        const auto layout=*DisplayLayout::create({{"DP-3",{0,0,520,500},1,"normal"},
                                                  {"eDP-1",{620,160,400,340},2.3,"270"}},
                                                 "DP-3",DisplayLayout::Mode::Extend);
        DesktopItems items(root.path(),profile.path());
        QVERIFY(write(items.directory()+"/Return.txt","original bytes"));
        items.refresh(); items.setDisplayLayout(layout);
        const auto identity=key(items,"Return.txt");
        items.select(identity); items.beginDrag();
        const auto start=point(items,"Return.txt");
        items.moveSelected(760-start.x(),250-start.y());
        items.finishDrag(800,280);
        QCOMPARE(point(items,"Return.txt"),QPoint(760,250));
        QVERIFY(items.beginSelection(100,100)); items.updateSelection(850,350);
        items.setDisplayLayout(DisplayLayout::single({520,500}));
        QVERIFY(!items.selecting());
        QVERIFY(QRect(0,40,520,460).contains(QRect(point(items,"Return.txt"),QSize(100,76))));
        QVERIFY(items.savePositions());
        items.select(identity); items.beginDrag(); items.moveSelected(-50,30);
        items.setDisplayLayout(layout); // Cancels the unfinished move, then restores preference.
        QVERIFY(!items.dragging());
        QCOMPARE(point(items,"Return.txt"),QPoint(760,250));
        items.setViewport(200,200); // A transient host configure is not a new topology.
        QCOMPARE(point(items,"Return.txt"),QPoint(760,250));
        DesktopItems restored(root.path(),profile.path()); restored.setDisplayLayout(layout);
        QCOMPARE(point(restored,"Return.txt"),QPoint(760,250));
        QFile file(restored.directory()+"/Return.txt"); QVERIFY(file.open(QIODevice::ReadOnly));
        QCOMPARE(file.readAll(),QByteArray("original bytes"));
    }
    void groupPlacementRescuesTogetherInsteadOfStrandingIconsInAGap()
    {
        QTemporaryDir root,profile;
        DesktopItems items(root.path(),profile.path());
        for(const auto *name:{"A.txt","B.txt"}) QVERIFY(write(items.directory()+'/'+name));
        items.refresh();
        const auto layout=*DisplayLayout::create({{"DP-3",{0,0,520,500},1,"normal"},
                                                  {"eDP-1",{620,160,400,340},2,"270"}},
                                                 "DP-3",DisplayLayout::Mode::Extend);
        items.setDisplayLayout(layout);
        int x=110;
        for(const auto *name:{"A.txt","B.txt"})
        {
            items.clearSelection(); items.select(key(items,name));
            const auto old=point(items,name);items.moveSelected(x-old.x(),120-old.y());x+=110;
        }
        items.select(key(items,"A.txt"));items.select(key(items,"B.txt"),true);
        QCOMPARE(items.selectionCount(),2);
        const auto offset=point(items,"B.txt")-point(items,"A.txt");
        items.beginDrag();items.moveSelected(420,40);items.finishDrag(680,180);
        QCOMPARE(point(items,"B.txt")-point(items,"A.txt"),offset);
        for(const auto *name:{"A.txt","B.txt"})
        {
            QVERIFY(QRect(620,174,400,326).contains(QRect(point(items,name),QSize(100,76))));
            QVERIFY(QFileInfo::exists(items.directory()+'/'+name));
        }
    }
    void initialOutputSizePlacesSystemIconsWithoutOverwritingSavedPositions()
    {
        QTemporaryDir root, profile;
        DesktopItems items(root.path(), profile.path(), nullptr, {2560, 1440});
        QVERIFY(point(items, "Hard Disk").x() > 2300);
        QVERIFY(point(items, "Trash").x() > 2300);
        QVERIFY(point(items, "Trash").y() > 1300);
        items.select("volume");
        items.moveSelected(150 - point(items, "Hard Disk").x(), 150 - 50);
        QVERIFY(items.savePositions());
        DesktopItems restored(root.path(), profile.path(), nullptr, {1920, 1080});
        QCOMPARE(point(restored, "Hard Disk"), QPoint(150, 150));
    }
    void marqueeSelectsInEitherDirectionExtendsAndRestoresWithoutMovingFiles()
    {
        QTemporaryDir root, profile;
        DesktopItems items(root.path(), profile.path());
        for (const auto &name : {"One.txt", "Two.txt", "Three.txt"})
            QVERIFY(write(items.directory() + '/' + name, name));
        items.refresh();
        int x = 180;
        for (const auto &name : {"One.txt", "Two.txt", "Three.txt"})
        {
            items.clearSelection();
            items.select(key(items, name));
            const auto old = point(items, name);
            items.moveSelected(x - old.x(), 220 - old.y());
            x += 150;
        }
        items.selectAll();
        QVERIFY(items.beginSelection(440, 310));
        QCOMPARE(items.selectionCount(), 0);
        items.updateSelection(170, 210); // Reverse-direction rectangle.
        QCOMPARE(items.selectionRect(), QRect(QPoint(170, 210), QPoint(440, 310)));
        QCOMPARE(items.selectionCount(), 2);
        items.endSelection();
        QVERIFY(!items.selecting());
        QVERIFY(items.beginSelection(470, 210, true));
        items.updateSelection(590, 310);
        QCOMPARE(items.selectionCount(), 3);
        items.cancelSelection();
        QCOMPARE(items.selectionCount(), 2);
        QVERIFY(items.beginSelection(470, 210, true));
        items.updateSelection(590, 310);
        items.endSelection();
        QCOMPARE(items.selectionCount(), 3);
        items.select(key(items, "One.txt")); // Pressing a selected icon retains the group.
        QCOMPARE(items.selectionCount(), 3);
        QVERIFY(items.beginSelection(20, 400));
        items.endSelection(); // Empty-space click clears selection.
        QCOMPARE(items.selectionCount(), 0);
        QVERIFY(items.beginSelection(0, 26));
        items.updateSelection(99999, 99999);
        QVERIFY(items.selectionRect().right() < 1120);
        QVERIFY(items.selectionRect().bottom() < 760);
        items.cancelSelection();
        QCOMPARE(items.selectionCount(), 0);
        x = 180;
        for (const auto &name : {"One.txt", "Two.txt", "Three.txt"})
        {
            QCOMPARE(point(items, name), QPoint(x, 220));
            QFile file(items.directory() + '/' + name);
            QVERIFY(file.open(QIODevice::ReadOnly));
            QCOMPARE(file.readAll(), QByteArray(name));
            x += 150;
        }
    }
    void copyingSelectedIconsCopiesTheirNamesInReadingOrder()
    {
        QTemporaryDir root, profile;
        DesktopItems items(root.path(), profile.path());
        for (const auto &name : {"Alpha.txt", "Beta.txt", "Gamma.txt"})
            QVERIFY(write(items.directory() + '/' + name, name));
        items.refresh();
        // Gamma above, Beta to the right of Alpha below it: reading order, not name order.
        const QList<std::tuple<QString, int, int>> places{
            {"Gamma.txt", 300, 120}, {"Alpha.txt", 180, 260}, {"Beta.txt", 420, 260}};
        for (const auto &[name, x, y] : places)
        {
            items.clearSelection();
            items.select(key(items, name));
            const auto old = point(items, name);
            items.moveSelected(x - old.x(), y - old.y());
        }
        items.clearSelection();
        QCOMPARE(items.selectedNames(), QString());
        items.select(key(items, "Beta.txt"));
        QCOMPARE(items.selectedNames(), QString("Beta.txt"));
        for (const auto &name : {"Alpha.txt", "Gamma.txt"})
            items.select(key(items, name), true);
        QCOMPARE(items.selectedNames(), QString("Gamma.txt\nAlpha.txt\nBeta.txt"));
    }
    void typingSelectsTheNearestNameLikeMacOS9()
    {
        // The names on the 9.2.2 desktop in psn_tch's VM, beside the disk and Trash. The disk is
        // "Hard Disk" unless the profile's finder.ini names it, as this one does.
        QTemporaryDir root, profile;
        QSettings(profile.path() + "/finder.ini", QSettings::IniFormat).setValue("diskName", "Macintosh HD");
        DesktopItems items(root.path(), profile.path());
        for (const auto &name : {"Browse the Internet", "Mail", "QuickTime Player", "Register with Apple",
                                 "Sherlock 2", "toot toot"})
            QVERIFY(write(items.directory() + '/' + name, name));
        items.refresh();
        const auto chosen = [&](const QString &typed) {
            items.clearSelection();
            return items.typeSelect(typed) ? items.selectedNames() : QString("-");
        };
        QCOMPARE(chosen("ma"), QString("Macintosh HD")); // "Mac…" sorts before "Mail"
        QCOMPARE(chosen("mai"), QString("Mail"));
        QCOMPARE(chosen("n"), QString("QuickTime Player")); // nothing starts with n: the next name
        QCOMPARE(chosen("TO"), QString("toot toot"));         // case does not matter
        QCOMPARE(chosen("zz"), QString("Trash"));             // past every name: the last one
        QCOMPARE(chosen(""), QString("-"));
        // A typed selection replaces a multiple selection.
        items.selectAll();
        QVERIFY(items.typeSelect("sh"));
        QCOMPARE(items.selectedNames(), QString("Sherlock 2"));
    }
    void filesystemChangesCancelMarqueeWithoutRestoringDeletedSelections()
    {
        QTemporaryDir root, profile;
        DesktopItems items(root.path(), profile.path());
        QVERIFY(write(items.directory() + "/Gone.txt"));
        items.refresh();
        items.select(key(items, "Gone.txt"));
        QVERIFY(items.beginSelection(0, 26));
        items.updateSelection(1119, 759);
        QVERIFY(QFile::remove(items.directory() + "/Gone.txt"));
        items.refresh();
        QVERIFY(!items.selecting());
        QCOMPARE(items.selectionCount(), 0);
    }
    void windowTransfersRetainSourcesOnCancelFailureAndConflictAndCopyOrMoveRealFiles()
    {
        QTemporaryDir root, profile;
        DesktopItems items(root.path(), profile.path());
        QVERIFY(QDir(root.path()).mkdir("Folder"));
        const auto source = items.directory() + "/Transfer.txt";
        const auto destination = root.path() + "/Folder/Transfer.txt";
        QVERIFY(write(source, "original bytes"));
        items.refresh();
        items.select(key(items, "Transfer.txt"));
        const auto original = point(items, "Transfer.txt");
        auto prepare = [&](bool copy)
        {
            items.beginDrag();
            items.moveSelected(-120, 80);
            return items.beginWindowDrop(copy);
        };
        QVERIFY(prepare(false));
        QVERIFY(items.busy());
        QCOMPARE(point(items, "Transfer.txt"), original);
        QVERIFY(!items.createFolder("Blocked"));
        QVERIFY(!QFileInfo(items.directory() + "/Blocked").exists());
        QVERIFY(items.completeWindowDrop({}));
        QVERIFY(!items.busy());
        QVERIFY(QFileInfo::exists(source));
        QVERIFY(!items.completeWindowDrop(root.path() + "/Folder"));
        QVERIFY(prepare(true));
        QVERIFY(items.completeWindowDrop(root.path() + "/Folder"));
        QTRY_VERIFY(!items.busy());
        QVERIFY(QFileInfo::exists(source));
        QVERIFY(QFileInfo::exists(destination));
        QSignalSpy errors(&items, &DesktopItems::errorOccurred);
        QVERIFY(prepare(false));
        QVERIFY(items.completeWindowDrop(root.path() + "/Folder"));
        QTRY_VERIFY(!items.busy());
        QCOMPARE(errors.count(), 1);
        QVERIFY(QFileInfo::exists(source));
        QFile copied(destination);
        QVERIFY(copied.open(QIODevice::ReadOnly));
        QCOMPARE(copied.readAll(), QByteArray("original bytes"));
        copied.close();
        QVERIFY(prepare(false));
        QVERIFY(!items.completeWindowDrop({}, "Disconnected folder"));
        QVERIFY(!items.busy());
        QCOMPARE(errors.count(), 2);
        QVERIFY(QFileInfo::exists(source));
        QVERIFY(QFile::remove(destination));
        QVERIFY(prepare(false));
        QVERIFY(items.completeWindowDrop(root.path() + "/Folder"));
        QTRY_VERIFY(!items.busy());
        QVERIFY(!QFileInfo::exists(source));
        QVERIFY(QFileInfo::exists(destination));
    }
    void folderDropsCopyMoveAndKeepPlacementOnCollision()
    {
        QTemporaryDir root, profile;
        DesktopItems items(root.path(), profile.path());
        QVERIFY(QDir(items.directory()).mkdir("Target"));
        const auto path = items.directory() + "/Drag me.txt";
        QVERIFY(write(path, "contents survive"));
        items.refresh();
        items.select(key(items, "Drag me.txt"));
        const auto original = point(items, "Drag me.txt"),
                   target = point(items, "Target") + QPoint(50, 20);
        auto drop = [&](bool copy)
        {
            items.beginDrag();
            items.moveSelected(-150, 40);
            items.finishDrag(target.x(), target.y(), copy);
        };
        drop(true);
        QTRY_VERIFY(!items.busy());
        QVERIFY(QFileInfo(path).isFile());
        QVERIFY(QFileInfo(items.directory() + "/Target/Drag me.txt").isFile());
        QCOMPARE(point(items, "Drag me.txt"), original);
        QSignalSpy errors(&items, &DesktopItems::errorOccurred);
        drop(false); // Existing destination is preserved rather than replaced.
        QTRY_VERIFY(!items.busy());
        QCOMPARE(errors.count(), 1);
        QVERIFY(QFileInfo(path).isFile());
        QCOMPARE(point(items, "Drag me.txt"), original);
        QVERIFY(QFile::remove(items.directory() + "/Target/Drag me.txt"));
        drop(false);
        QTRY_VERIFY(!items.busy());
        QVERIFY(!QFileInfo(path).exists());
        QFile moved(items.directory() + "/Target/Drag me.txt");
        QVERIFY(moved.open(QIODevice::ReadOnly));
        QCOMPARE(moved.readAll(), QByteArray("contents survive"));
    }
    void trashDropIsRecoverableAndCancelledDragOnlyRestoresPlacement()
    {
        QTemporaryDir root, profile;
        DesktopItems items(root.path(), profile.path());
        const auto path = items.directory() + "/Recover me.txt";
        QVERIFY(write(path, "recoverable"));
        items.refresh();
        items.select(key(items, "Recover me.txt"));
        const auto original = point(items, "Recover me.txt");
        items.beginDrag();
        items.moveSelected(-200, 90);
        items.refresh();
        QVERIFY(items.dragging());
        QCOMPARE(point(items, "Recover me.txt"), original + QPoint(-200, 90));
        items.cancelDrag();
        QCOMPARE(point(items, "Recover me.txt"), original);
        QVERIFY(QFileInfo(path).isFile());
        const auto target = point(items, "Trash") + QPoint(50, 20);
        QSignalSpy completed(&items, &DesktopItems::operationFinished);
        items.beginDrag();
        items.moveSelected(-200, 90);
        items.finishDrag(target.x(), target.y());
        QTRY_VERIFY(!items.busy());
        QVERIFY(completed.last()[0].toBool());
        QVERIFY(!QFileInfo(path).exists());
        const auto trash = completed.last()[1].toStringList().first();
        FileOperations operations(root.path(), profile.path() + "/data/Trash");
        QVERIFY(operations.restore(trash).ok);
        QVERIFY(QFileInfo(path).isFile());
    }
    void fileJobsCreateRenameDuplicateAndTrashRealItems()
    {
        QTemporaryDir root, profile;
        DesktopItems items(root.path(), profile.path());
        QSignalSpy completed(&items, &DesktopItems::operationFinished);
        QVERIFY(items.createFolder("New desktop folder"));
        QVERIFY(items.busy());
        QTRY_VERIFY(!items.busy());
        QVERIFY(QFileInfo(items.directory() + "/New desktop folder").isDir());
        QCOMPARE(completed.count(), 1);
        QVERIFY(completed.last()[0].toBool());
        QVERIFY(write(items.directory() + "/Document.txt", "preserved contents"));
        items.refresh();
        const auto identity = key(items, "Document.txt");
        items.select(identity);
        items.moveSelected(-300, 30);
        const auto placed = point(items, "Document.txt");
        QVERIFY(items.renameItem(identity, "Renamed.txt"));
        QTRY_VERIFY(!items.busy());
        QCOMPARE(key(items, "Renamed.txt"), identity);
        QCOMPARE(point(items, "Renamed.txt"), placed);
        QCOMPARE(items.selectionCount(), 1);
        QVERIFY(items.duplicateSelected());
        QTRY_VERIFY(!items.busy());
        QVERIFY(QFileInfo(items.directory() + "/Renamed copy.txt").isFile());
        QCOMPARE(items.renameTarget().value("name").toString(), QString("Renamed copy.txt"));
        QVERIFY(items.trashSelected());
        QTRY_VERIFY(!items.busy());
        const auto trashed = completed.last()[1].toStringList().first();
        QVERIFY(QFileInfo(trashed).isFile());
        QVERIFY(!QFileInfo(items.directory() + "/Renamed copy.txt").exists());
        FileOperations operations(root.path(), profile.path() + "/data/Trash");
        const auto restored = operations.restore(trashed);
        QVERIFY2(restored.ok, qPrintable(restored.error));
        QFile content(restored.path);
        QVERIFY(content.open(QIODevice::ReadOnly));
        QCOMPARE(content.readAll(), QByteArray("preserved contents"));
        DesktopItems reloaded(root.path(), profile.path());
        QCOMPARE(point(reloaded, "Renamed.txt"), placed);
    }
    void collisionsAndSpecialObjectsCannotBecomeDestructiveSelections()
    {
        QTemporaryDir root, profile;
        DesktopItems items(root.path(), profile.path());
        QVERIFY(write(items.directory() + "/Keep.txt", "keep"));
        QVERIFY(write(items.directory() + "/Occupied.txt", "occupied"));
        items.refresh();
        QSignalSpy errors(&items, &DesktopItems::errorOccurred),
            completed(&items, &DesktopItems::operationFinished);
        items.select(key(items, "Keep.txt"));
        QVERIFY(items.renameItem(key(items, "Keep.txt"), "Occupied.txt"));
        QTRY_VERIFY(!items.busy());
        QVERIFY(!completed.last()[0].toBool());
        QCOMPARE(errors.count(), 1);
        QVERIFY(QFileInfo(items.directory() + "/Keep.txt").isFile());
        QFile occupied(items.directory() + "/Occupied.txt");
        QVERIFY(occupied.open(QIODevice::ReadOnly));
        QCOMPARE(occupied.readAll(), QByteArray("occupied"));
        items.select("trash", true);
        QVERIFY(!items.canModifySelection());
        QVERIFY(!items.trashSelected());
        QVERIFY(!items.renameItem("volume", "Danger"));
        QVERIFY(!items.busy());
        QVERIFY(QFileInfo(items.directory() + "/Keep.txt").isFile());
        QVERIFY(items.createFolder("../escape"));
        QTRY_VERIFY(!items.busy());
        QVERIFY(!completed.last()[0].toBool());
        QVERIFY(!QFileInfo(root.path() + "/escape").exists());
    }
    void partialBatchFailureKeepsSuccessfulCopiesAndReportsItsCount()
    {
        QTemporaryDir root, profile;
        DesktopItems items(root.path(), profile.path());
        QVERIFY(write(items.directory() + "/A.txt"));
        QVERIFY(write(items.directory() + "/B.txt"));
        items.refresh();
        items.select(key(items, "A.txt"));
        items.select(key(items, "B.txt"), true);
        QVERIFY(QFile::remove(items.directory() + "/B.txt"));
        QSignalSpy completed(&items, &DesktopItems::operationFinished),
            errors(&items, &DesktopItems::errorOccurred);
        QVERIFY(items.duplicateSelected());
        QVERIFY(!items.duplicateSelected()); // A second worker cannot race the first.
        QTRY_VERIFY(!items.busy());
        QVERIFY(!completed.last()[0].toBool());
        QCOMPARE(completed.last()[1].toStringList().size(), 1);
        QVERIFY(QFileInfo(items.directory() + "/A copy.txt").isFile());
        QVERIFY(errors.last()[0].toString().contains("1 of 2"));
    }
    void atomicSavePreservesPlacementWithoutConfusingRenameAndReplacement()
    {
        QTemporaryDir root, profile;
        DesktopItems items(root.path(), profile.path());
        const auto path = items.directory() + "/Document.txt";
        QVERIFY(write(path, "before"));
        items.refresh();
        const auto identity = key(items, "Document.txt");
        items.select(identity);
        items.moveSelected(-330, 40);
        const auto placed = point(items, "Document.txt");
        QSaveFile replacement(path);
        QVERIFY(replacement.open(QIODevice::WriteOnly));
        QCOMPARE(replacement.write("after"), 5);
        QVERIFY(replacement.commit());
        items.refresh();
        QCOMPARE(key(items, "Document.txt"), identity);
        QCOMPARE(point(items, "Document.txt"), placed);
        QCOMPARE(items.selectionCount(), 1);
        QVERIFY(items.savePositions());
        DesktopItems restored(root.path(), profile.path());
        QCOMPARE(point(restored, "Document.txt"), placed);
        QVERIFY(QFile::rename(path, items.directory() + "/Renamed.txt"));
        QVERIFY(write(path, "new file at the old name"));
        restored.refresh();
        const auto renamed = key(restored, "Renamed.txt"),
                   replacementKey = key(restored, "Document.txt");
        QVERIFY(renamed != replacementKey);
        if (renamed.startsWith("file:"))
        {
            QCOMPARE(renamed, identity);
            QCOMPARE(point(restored, "Renamed.txt"), placed);
        }
    }
    void realEntriesAndSelectionOpenTheActualPaths()
    {
        QTemporaryDir root, profile;
        DesktopItems items(root.path(), profile.path());
        QVERIFY(QDir(items.directory()).mkdir("Documents"));
        QVERIFY(write(items.directory() + "/Hello.txt"));
        items.refresh();
        QCOMPARE(items.rowCount(), 4);
        const auto folder = named(items, "Documents");
        QCOMPARE(items.data(folder, DesktopItems::KindRole).toString(), QString("folder"));
        items.select(key(items, "Hello.txt"));
        items.select(key(items, "Documents"), true);
        QCOMPARE(items.selectionCount(), 2);
        QSignalSpy opened(&items, &DesktopItems::openRequested);
        items.openSelected();
        QCOMPARE(opened.count(), 2);
        QSet<QString> paths;
        for (const auto &call : opened)
            paths.insert(call[1].toString());
        QCOMPARE(paths, (QSet<QString>{items.directory() + "/Hello.txt",
                                       items.directory() + "/Documents"}));
        items.select("unknown", false);
        QCOMPARE(items.selectionCount(), 2);
        items.clearSelection();
        QCOMPARE(items.selectionCount(), 0);
        QVERIFY(QFileInfo(items.directory() + "/Hello.txt").isFile());
    }
    void getInfoDetailsAreTruthfulAndReadOnly()
    {
        // Finishing School FS-G01: one item's facts, read once; nothing written; a folder shows
        // how many items it holds (shallow), never its own inode size; an alias names its original.
        QTemporaryDir root, profile;
        DesktopItems items(root.path(), profile.path());
        const auto desktop = items.directory();
        QVERIFY(write(desktop + "/Hello.txt", "hello, world"));
        QVERIFY(QDir(desktop).mkpath("Documents/Inner"));
        QVERIFY(write(desktop + "/Documents/a.txt"));
        // A relative link, as aliases made inside the folder usually are.
        QVERIFY(QFile::link("Hello.txt", desktop + "/Hello alias"));
        items.refresh();
        const auto listing = [&] { return QDir(desktop).entryList(QDir::AllEntries | QDir::NoDotAndDotDot | QDir::System); };
        const auto before = listing();
        const auto details = [&](const QString &name)
        {
            items.clearSelection();
            items.select(key(items, name));
            return items.selectedDetails();
        };
        auto info = details("Hello.txt");
        QCOMPARE(info.value("name").toString(), QString("Hello.txt"));
        QCOMPARE(info.value("kindLabel").toString(), QString("Plain text document"));
        QCOMPARE(info.value("size").toString(), QString("12 bytes (12 bytes)"));
        QCOMPARE(info.value("where").toString(), desktop);
        QVERIFY(info.value("modified").toString().contains(QString::number(QDate::currentDate().year())));
        const auto created = info.value("created").toString();
        QVERIFY(created == "Unavailable" || created.contains(QString::number(QDate::currentDate().year())));
        info = details("Documents");
        QCOMPARE(info.value("kindLabel").toString(), QString("Folder"));
        QCOMPARE(info.value("size").toString(), QString("Contains 2 items"));
        info = details("Hello alias");
        QCOMPARE(info.value("kindLabel").toString(), QString("Alias"));
        QCOMPARE(info.value("original").toString(), desktop + "/Hello.txt");
        QVERIFY(!info.contains("size"));
        info = details("Hard Disk");
        QCOMPARE(info.value("kindLabel").toString(), QString("Disk"));
        QVERIFY(info.value("capacity").toString().contains(" bytes)"));
        QVERIFY(info.value("available").toString().contains(" bytes)"));
        QCOMPARE(details("Trash").value("kindLabel").toString(), QString("Trash"));
        // Not one item: nothing to show.
        items.select(key(items, "Documents"), true);
        QVERIFY(items.selectedDetails().isEmpty());
        items.clearSelection();
        QVERIFY(items.selectedDetails().isEmpty());
        QCOMPARE(listing(), before);
    }
    void groupPlacementSurvivesRestartAndViewportChanges()
    {
        QTemporaryDir root, profile;
        DesktopItems items(root.path(), profile.path());
        QVERIFY(write(items.directory() + "/One.txt"));
        QVERIFY(write(items.directory() + "/Two.txt"));
        items.refresh();
        const auto one = point(items, "One.txt"), two = point(items, "Two.txt");
        items.select(key(items, "One.txt"));
        items.select(key(items, "Two.txt"), true);
        items.moveSelected(-400, 30);
        const auto moved = point(items, "One.txt");
        QCOMPARE(moved, one + QPoint(-400, 30));
        QCOMPARE(point(items, "Two.txt") - moved, two - one);
        QVERIFY(items.savePositions());
        DesktopItems restored(root.path(), profile.path());
        QCOMPARE(point(restored, "One.txt"), moved);
        restored.setViewport(480, 320);
        const auto small = point(restored, "One.txt");
        QVERIFY(small.x() >= 0 && small.x() <= 380);
        QVERIFY(small.y() >= 40 && small.y() <= 244);
        restored.setViewport(1120, 760);
        QCOMPARE(point(restored, "One.txt"), moved);
        restored.selectAll();
        restored.moveSelected(-100000, 100000);
        for (int row = 0; row < restored.rowCount(); ++row)
        {
            const auto p = point(
                restored, restored.data(restored.index(row), DesktopItems::NameRole).toString());
            QVERIFY(p.x() >= 0 && p.x() <= 1020);
            QVERIFY(p.y() >= 40 && p.y() <= 684);
        }
    }
    void externalRenameKeepsBirthIdentityAndHardLinksStayDistinct()
    {
        QTemporaryDir root, profile;
        DesktopItems items(root.path(), profile.path());
        const auto old = items.directory() + "/Before.txt";
        QVERIFY(write(old));
        items.refresh();
        const auto identity = key(items, "Before.txt");
        items.select(identity);
        items.moveSelected(-270, 20);
        const auto placed = point(items, "Before.txt");
        QVERIFY(QFile::rename(old, items.directory() + "/After.txt"));
        items.refresh();
        if (identity.startsWith("file:"))
        {
            QCOMPARE(key(items, "After.txt"), identity);
            QCOMPARE(point(items, "After.txt"), placed);
            QCOMPARE(items.selectionCount(), 1);
        }
        else
            qInfo("Filesystem has no birth time; name-based fallback deliberately has no rename "
                  "continuity.");
        QVERIFY(::link(QFile::encodeName(items.directory() + "/After.txt").constData(),
                       QFile::encodeName(items.directory() + "/Linked.txt").constData()) == 0);
        items.refresh();
        QVERIFY(key(items, "After.txt") != key(items, "Linked.txt"));
        items.select(key(items, "After.txt"));
        items.select(key(items, "Linked.txt"), true);
        QCOMPARE(items.selectionCount(), 2);
    }
    void filesystemWatcherRemovesStaleSelection()
    {
        QTemporaryDir root, profile;
        DesktopItems items(root.path(), profile.path());
        const auto path = items.directory() + "/Arrived.txt";
        QVERIFY(write(path));
        QTRY_VERIFY(named(items, "Arrived.txt").isValid());
        items.select(key(items, "Arrived.txt"));
        QCOMPARE(items.selectionCount(), 1);
        QVERIFY(QFile::remove(path));
        QTRY_VERIFY(!named(items, "Arrived.txt").isValid());
        QCOMPARE(items.selectionCount(), 0);
    }
    void folderToDesktopDropsMoveCopyPlaceAndPreserveConflicts()
    {
        QTemporaryDir root, profile, outside;
        DesktopItems items(root.path(), profile.path());
        QVERIFY(QDir(root.path()).mkdir("Source"));
        const auto source = root.path() + "/Source/Dragged.txt";
        QVERIFY(write(source, "preserved contents"));
        QSignalSpy completed(&items, &DesktopItems::operationFinished);
        QSignalSpy errors(&items, &DesktopItems::errorOccurred);
        QVERIFY(items.receiveFiles({source}, {260, 450}, false));
        QTRY_COMPARE(completed.count(), 1);
        QVERIFY(completed.last().first().toBool());
        QVERIFY(!QFileInfo::exists(source));
        QVERIFY(QFileInfo::exists(items.directory() + "/Dragged.txt"));
        QCOMPARE(point(items, "Dragged.txt"), QPoint(210, 430));
        QVERIFY(items.receiveFiles({items.directory() + "/Dragged.txt"}, {250, 450}, false));
        QVERIFY(!items.busy());
        QCOMPARE(completed.count(), 1); // Same-directory move is harmless.
        QVERIFY(write(source, "second version stays here"));
        QVERIFY(items.receiveFiles({source}, {360, 480}, false));
        QTRY_COMPARE(completed.count(), 2);
        QVERIFY(!completed.last().first().toBool());
        QCOMPARE(errors.count(), 1);
        QFile original(source), target(items.directory() + "/Dragged.txt");
        QVERIFY(original.open(QIODevice::ReadOnly));
        QCOMPARE(original.readAll(), QByteArray("second version stays here"));
        original.close();
        QVERIFY(target.open(QIODevice::ReadOnly));
        QCOMPARE(target.readAll(), QByteArray("preserved contents"));
        target.close();
        QVERIFY(write(root.path() + "/Source/Copy.txt", "copy bytes"));
        QVERIFY(items.receiveFiles({root.path() + "/Source/Copy.txt"}, {410, 450}, true));
        QTRY_COMPARE(completed.count(), 3);
        QVERIFY(completed.last().first().toBool());
        QVERIFY(QFileInfo::exists(root.path() + "/Source/Copy.txt"));
        QVERIFY(QFileInfo::exists(items.directory() + "/Copy.txt"));
        QVERIFY(write(outside.path() + "/Outside.txt"));
        QVERIFY(!items.receiveFiles({source, outside.path() + "/Outside.txt"}, {300, 500}, false));
        QVERIFY(!items.busy());
        QCOMPARE(completed.count(), 3);
        QVERIFY(QFileInfo::exists(source));
        QVERIFY(QDir(items.directory()).mkdir("Destination"));
        items.refresh();
        QVERIFY(items.receiveFiles({source}, point(items, "Destination") + QPoint(30, 20), false));
        QTRY_COMPARE(completed.count(), 4);
        QVERIFY(completed.last().first().toBool());
        QVERIFY(QFileInfo::exists(items.directory() + "/Destination/Dragged.txt"));
        QVERIFY(!QFileInfo::exists(source));
        const auto trashSource = root.path() + "/Source/Trash me.txt";
        QVERIFY(write(trashSource));
        QVERIFY(items.receiveFiles({trashSource}, point(items, "Trash") + QPoint(30, 20), false));
        QTRY_COMPARE(completed.count(), 5);
        QVERIFY(completed.last().first().toBool());
        QVERIFY(!QFileInfo::exists(trashSource));
        QVERIFY(QFileInfo::exists(profile.path() + "/data/Trash/files/Trash me.txt"));
    }
    void foreignStateAndOutsideDesktopSymlinkAreNotAdopted()
    {
        QTemporaryDir root, profile, outside;
        QVERIFY(write(outside.path() + "/Private.txt"));
        QVERIFY(QFile::link(outside.path(), root.path() + "/Desktop"));
        QVERIFY(
            write(profile.path() + "/desktop.json",
                  QJsonDocument(
                      QJsonObject{{"version", 1},
                                  {"workspace", outside.path()},
                                  {"positions", QJsonObject{{"volume", QJsonArray{99999, -1000}}}}})
                      .toJson()));
        DesktopItems items(root.path(), profile.path());
        QCOMPARE(items.rowCount(), 2);
        QVERIFY(!named(items, "Private.txt").isValid());
        QVERIFY(point(items, "Hard Disk").x() < 1120);
        QVERIFY(point(items, "Hard Disk").y() >= 40);
        QVERIFY(QFileInfo(outside.path() + "/Private.txt").isFile());
    }
};
QTEST_GUILESS_MAIN(DesktopItemsTests)
#include "desktop_items_tests.moc"
