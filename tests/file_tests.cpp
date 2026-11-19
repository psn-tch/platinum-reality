#include <gio/gio.h>
#include "finder/file_operations.hpp"
#include "finder/recent_documents.hpp"
#include <QTemporaryDir>
#include <QtTest>
#include <QScopeGuard>
#include <functional>
#include <fcntl.h>
#include <memory>
#include <sys/stat.h>
#include <unistd.h>

namespace
{
QString mutateAfterCopying;
std::function<void()> afterCopy, afterPublish;
QString mutateDuringMoving;
std::function<void()> duringMove;
}

extern "C" gboolean __real_g_file_copy(GFile *, GFile *, GFileCopyFlags, GCancellable *,
                                       GFileProgressCallback, gpointer, GError **);
extern "C" gboolean __real_g_file_move(GFile *, GFile *, GFileCopyFlags, GCancellable *,
                                       GFileProgressCallback, gpointer, GError **);

// Execute real GIO operations. These test-only link wrappers place an edit at an exact
// boundary, without sleeps, large payloads, or a production callback/test bypass.
extern "C" gboolean __wrap_g_file_copy(GFile *source, GFile *target, GFileCopyFlags flags,
                                       GCancellable *cancel, GFileProgressCallback progress,
                                       gpointer data, GError **error)
{
    const auto ok = __real_g_file_copy(source, target, flags, cancel, progress, data, error);
    if (ok && afterCopy)
    {
        char *path = g_file_get_path(source);
        const bool matches = QFile::decodeName(path) == mutateAfterCopying;
        g_free(path);
        if (matches)
        {
            auto edit = std::move(afterCopy);
            afterCopy = {};
            edit();
        }
    }
    return ok;
}

extern "C" gboolean __wrap_g_file_move(GFile *source, GFile *target, GFileCopyFlags flags,
                                       GCancellable *cancel, GFileProgressCallback progress,
                                       gpointer data, GError **error)
{
    char *path = g_file_get_path(source);
    const bool publishing = QFileInfo(QFile::decodeName(path)).fileName().startsWith(".frankenstein-move-");
    const bool watching = QFile::decodeName(path) == mutateDuringMoving && bool(duringMove);
    g_free(path);
    const auto hook = [](goffset current, goffset total, gpointer) {
        if (current >= total && total > 0 && duringMove)
        {
            auto edit = std::move(duringMove);
            duringMove = {};
            edit();
        }
    };
    const auto ok = __real_g_file_move(source, target, flags, cancel, watching ? hook : progress,
                                      watching ? nullptr : data, error);
    if (ok && publishing && afterPublish)
    {
        auto edit = std::move(afterPublish);
        afterPublish = {};
        edit();
    }
    return ok;
}

class FileTests : public QObject
{
    Q_OBJECT
    static void write(const QString &p, const QByteArray &bytes)
    {
        QFile f(p);
        QVERIFY(f.open(QIODevice::WriteOnly));
        QCOMPARE(f.write(bytes), bytes.size());
    }
  private slots:
    void recentDocumentsKeepOrderAndTrimOnlyAliases()
    {
        QTemporaryDir root;
        const auto profile = root.filePath("profile");
        QDir().mkpath(root.filePath("other"));
        const auto first = root.filePath("Report.txt"), second = root.filePath("other/Report.txt"),
                   hidden = root.filePath(".env");
        for (const auto &path : {first, second, hidden}) write(path, "document contents");
        QVERIFY(RecentDocuments::remember(profile, first, 2));
        QVERIFY(RecentDocuments::remember(profile, second, 2));
        auto list = RecentDocuments::list(profile);
        QCOMPARE(list.size(), 2);
        QCOMPARE(list[0].toMap().value("target").toString(), second);
        QVERIFY(list[0].toMap().value("link") != list[1].toMap().value("link"));
        QVERIFY(RecentDocuments::remember(profile, first, 2));
        const auto trimmedAlias = RecentDocuments::list(profile)[1].toMap().value("link").toString();
        QVERIFY(RecentDocuments::remember(profile, hidden, 2));
        list = RecentDocuments::list(profile);
        QCOMPARE(list.size(), 2);
        QCOMPARE(list[0].toMap().value("name").toString(), QString(".env"));
        QVERIFY(!QFileInfo(list[0].toMap().value("link").toString()).fileName().startsWith('.'));
        QCOMPARE(list[1].toMap().value("target").toString(), first);
        QVERIFY(!QFileInfo(trimmedAlias).isSymLink());
        for (const auto &path : {first, second, hidden}) QVERIFY(QFileInfo(path).isFile());
    }
    void recentDocumentsCanBeForgottenWithoutDeletingDocuments()
    {
        QTemporaryDir root;
        const auto profile = root.filePath("profile"), first = root.filePath("One"), second = root.filePath("Two");
        write(first, "one"); write(second, "two");
        QVERIFY(RecentDocuments::remember(profile, first, 10));
        QVERIFY(RecentDocuments::remember(profile, second, 10));
        QVERIFY(QFile::remove(RecentDocuments::list(profile)[1].toMap().value("link").toString()));
        QCOMPARE(RecentDocuments::list(profile).size(), 1);
        // A missing target remains visible but disabled, and its dangling alias can be cleared.
        QVERIFY(QFile::remove(second));
        const auto list = RecentDocuments::list(profile);
        QCOMPARE(list.size(), 1);
        QVERIFY(!list[0].toMap().value("exists").toBool());
        const auto custom = RecentDocuments::folder(profile) + "/Keep Me";
        write(custom, "custom file");
        QVERIFY(RecentDocuments::clear(profile));
        QVERIFY(RecentDocuments::list(profile).isEmpty());
        QVERIFY(!QFileInfo(list[0].toMap().value("link").toString()).isSymLink());
        QVERIFY(QFileInfo(first).isFile());
        QVERIFY(QFileInfo(custom).isFile());
    }
    void recentDocumentsRejectInvalidItemsAndRollBackFailedSaves()
    {
        QTemporaryDir root;
        const auto profile = root.filePath("profile"), document = root.filePath("Report");
        write(document, "report");
        QVERIFY(!RecentDocuments::remember(profile, document, 0));
        QVERIFY(!RecentDocuments::remember(profile, document, 100));
        QVERIFY(!RecentDocuments::remember(profile, root.path(), 10));
        QVERIFY(!RecentDocuments::remember(profile, root.filePath("missing"), 10));
        // A directory occupying the JSON path forces a real atomic-save failure.
        QVERIFY(QDir().mkpath(profile + "/recent-documents.json"));
        QVERIFY(!RecentDocuments::remember(profile, document, 10));
        QVERIFY(QDir(RecentDocuments::folder(profile)).entryList(QDir::Files | QDir::System).isEmpty());
        QVERIFY(QFileInfo(document).isFile());
    }
    void recentDocumentsPreserveUserAliasesForTheSameTarget()
    {
        QTemporaryDir root;
        const auto document = root.filePath("Report"), profile = root.filePath("profile");
        write(document, "contents");
        QVERIFY(QDir().mkpath(RecentDocuments::folder(profile)));
        const auto existing = RecentDocuments::folder(profile) + "/My Alias";
        QVERIFY(QFile::link(document, existing));
        QVERIFY(RecentDocuments::remember(profile, document, 10));
        QVERIFY(RecentDocuments::clear(profile));
        QVERIFY(QFileInfo(existing).isSymLink());
        // A second alias added after recording must not be mistaken for the managed one.
        const auto other = root.filePath("other-profile");
        QVERIFY(RecentDocuments::remember(other, document, 10));
        const auto managed = RecentDocuments::list(other)[0].toMap().value("link").toString();
        const auto custom = RecentDocuments::folder(other) + "/ZZZ Custom Alias";
        QVERIFY(QFile::link(document, custom));
        QVERIFY(RecentDocuments::clear(other));
        QVERIFY(!QFileInfo(managed).isSymLink());
        QVERIFY(QFileInfo(custom).isSymLink());
        QVERIFY(QFileInfo(document).isFile());
    }
    void duplicateKeepsDotfilesHidden()
    {
        QTemporaryDir dir;
        write(dir.filePath(".env"), "private settings");
        FileOperations ops(dir.path(), dir.filePath("Trash"));
        const auto copied = ops.duplicate(dir.filePath(".env"));
        QVERIFY2(copied.ok, qPrintable(copied.error));
        QCOMPARE(QFileInfo(copied.path).fileName(), QString(".env copy"));
        QFile content(copied.path);
        QVERIFY(content.open(QIODevice::ReadOnly));
        QCOMPARE(content.readAll(), QByteArray("private settings"));
        const auto again = ops.duplicate(dir.filePath(".env"));
        QVERIFY2(again.ok, qPrintable(again.error));
        QCOMPARE(QFileInfo(again.path).fileName(), QString(".env copy 2"));
        write(dir.filePath(".settings.json"), "{}");
        const auto withSuffix = ops.duplicate(dir.filePath(".settings.json"));
        QVERIFY2(withSuffix.ok, qPrintable(withSuffix.error));
        QCOMPARE(QFileInfo(withSuffix.path).fileName(), QString(".settings copy.json"));
    }
    void newDotfilesKeepTheirLeadingDotWhenTheNameAlreadyExists()
    {
        QTemporaryDir dir;
        write(dir.filePath(".env"), "keep this");
        FileOperations ops(dir.path(), dir.filePath("Trash"));
        const auto made = ops.createFile(dir.path(), ".env");
        QVERIFY2(made.ok, qPrintable(made.error));
        QCOMPARE(QFileInfo(made.path).fileName(), QString(".env 2"));
        QFile original(dir.filePath(".env"));
        QVERIFY(original.open(QIODevice::ReadOnly));
        QCOMPARE(original.readAll(), QByteArray("keep this"));
        write(dir.filePath(".settings.json"), "{}");
        const auto withSuffix = ops.createFile(dir.path(), ".settings.json");
        QVERIFY2(withSuffix.ok, qPrintable(withSuffix.error));
        QCOMPARE(QFileInfo(withSuffix.path).fileName(), QString(".settings 2.json"));
    }
    void duplicateKeepsDocumentAssociationSuffixAndExistingCopies()
    {
        QTemporaryDir dir;
        const auto path = dir.path() + "/Report.txt";
        write(path, "original");
        FileOperations ops(dir.path(), dir.path() + "/Trash");
        const auto first = ops.duplicate(path), second = ops.duplicate(path);
        QVERIFY(first.ok);
        QVERIFY(second.ok);
        QCOMPARE(QFileInfo(first.path).fileName(), QString("Report copy.txt"));
        QCOMPARE(QFileInfo(second.path).fileName(), QString("Report copy 2.txt"));
        for (const auto &file : {path, first.path, second.path})
        {
            QFile content(file);
            QVERIFY(content.open(QIODevice::ReadOnly));
            QCOMPARE(content.readAll(), QByteArray("original"));
        }
    }
    void copyAndConflicts()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        QDir root(dir.path());
        QVERIFY(root.mkdir("destination"));
        write(root.filePath("source.txt"), "classic data");
        FileOperations ops(dir.path(), root.filePath("Trash"));
        auto copy = ops.copy(root.filePath("source.txt"), root.filePath("destination"));
        QVERIFY2(copy.ok, qPrintable(copy.error));
        QFile result(copy.path);
        QVERIFY(result.open(QIODevice::ReadOnly));
        QCOMPARE(result.readAll(), QByteArray("classic data"));
        QVERIFY(!ops.copy(root.filePath("source.txt"), root.filePath("destination")).ok);
        QFile source(root.filePath("source.txt"));
        QVERIFY(source.exists());
    }
    void directoryCopyDoesNotFollowSymlinks()
    {
        QTemporaryDir dir;
        QDir root(dir.path());
        root.mkdir("folder");
        root.mkdir("destination");
        write(root.filePath("folder/file"), "original");
        QVERIFY(QFile::link("file", root.filePath("folder/link")));
        FileOperations ops(dir.path(), root.filePath("Trash"));
        auto r = ops.copy(root.filePath("folder"), root.filePath("destination"));
        QVERIFY2(r.ok, qPrintable(r.error));
        QVERIFY(QFileInfo(root.filePath("destination/folder/link")).isSymLink());
        QVERIFY(!ops.copy(root.filePath("folder"), root.filePath("folder")).ok);
    }
    void renameDuplicateMove()
    {
        QTemporaryDir dir;
        QDir root(dir.path());
        root.mkdir("destination");
        write(root.filePath("file"), "body");
        FileOperations ops(dir.path(), root.filePath("Trash"));
        auto d = ops.duplicate(root.filePath("file"));
        QVERIFY2(d.ok, qPrintable(d.error));
        auto r = ops.rename(d.path, "renamed");
        QVERIFY(r.ok);
        QVERIFY(!QFileInfo(d.path).exists());
        auto m = ops.move(r.path, root.filePath("destination"));
        QVERIFY(m.ok);
        QVERIFY(QFileInfo(m.path).exists());
        QVERIFY(!ops.rename(root.filePath("file"), "../escape").ok);
    }
    void trashAndRestoreCollision()
    {
        QTemporaryDir dir;
        QDir root(dir.path());
        write(root.filePath("document"), "first");
        FileOperations ops(dir.path(), root.filePath("Trash"));
        auto t = ops.trash(root.filePath("document"));
        QVERIFY2(t.ok, qPrintable(t.error));
        QVERIFY(!QFileInfo(root.filePath("document")).exists());
        write(root.filePath("document"), "second");
        QVERIFY(!ops.restore(t.path).ok);
        QVERIFY(QFileInfo(t.path).exists());
        QFile::remove(root.filePath("document"));
        auto r = ops.restore(t.path);
        QVERIFY2(r.ok, qPrintable(r.error));
        QFile f(r.path);
        QVERIFY(f.open(QIODevice::ReadOnly));
        QCOMPARE(f.readAll(), QByteArray("first"));
    }
    void aDrivesFolderCreatedLaterIsStillAllowed()
    {
        // /run/media/<user> appears with the first mounted drive, after the desktop started.
        QTemporaryDir home, outside;
        const auto media = outside.path() + "/media/someone";
        FileOperations ops(home.path(), home.path() + "/Trash", {media});
        QVERIFY(QDir().mkpath(media + "/STICK"));
        QVERIFY(ops.readableDirectory(media + "/STICK"));
        QVERIFY(!ops.readableDirectory(outside.path()));
    }
    void confinementRejectsSymlinkedParent()
    {
        QTemporaryDir root;
        QTemporaryDir outside;
        write(outside.path() + "/valuable", "keep");
        QVERIFY(QFile::link(outside.path(), root.path() + "/escape"));
        FileOperations ops(root.path(), root.path() + "/Trash");
        QVERIFY(!ops.rename(root.path() + "/escape/valuable", "changed").ok);
        QVERIFY(!ops.trash(root.path() + "/escape/valuable").ok);
        QVERIFY(QFileInfo(outside.path() + "/valuable").exists());
        QVERIFY(!ops.readableDirectory(root.path() + "/escape"));
    }
    void mountedDrivesAreFurtherRootsButNotWritableThemselves()
    {
        // Home plus the mounted-drives folder: copies and moves work between them, the drive
        // folder itself cannot be renamed or trashed, and anything else stays outside.
        QTemporaryDir home, drives, outside;
        write(home.path() + "/note", "hello");
        QVERIFY(QDir(drives.path()).mkdir("USB"));
        write(outside.path() + "/private", "keep");
        FileOperations ops(home.path(), home.path() + "/.local/share/Trash", {drives.path(), home.path() + "/missing"});
        QVERIFY(ops.copy(home.path() + "/note", drives.path() + "/USB").ok);
        QVERIFY(QFileInfo(drives.path() + "/USB/note").isFile());
        QVERIFY(ops.move(drives.path() + "/USB/note", home.path() + "/..").ok == false);
        QVERIFY(ops.rename(drives.path() + "/USB/note", "renamed").ok);
        QVERIFY(ops.readableDirectory(drives.path() + "/USB"));
        QVERIFY(!ops.rename(drives.path(), "gone").ok);
        QVERIFY(!ops.trash(drives.path()).ok);
        QVERIFY(!ops.copy(outside.path() + "/private", home.path()).ok);
        QVERIFY(!ops.readableDirectory(outside.path()));
        QVERIFY(!ops.copy(home.path() + "/note", outside.path()).ok);
        // The Trash inside home, and home itself, can never be trashed.
        QVERIFY(ops.trash(home.path() + "/note").ok);
        QVERIFY(!ops.trash(home.path() + "/.local").ok);
        QVERIFY(!ops.trash(home.path()).ok);
    }
    void copyReportsProgressThatAddsUpToTheMeasuredTotal()
    {
        QTemporaryDir dir;
        QDir root(dir.path());
        QVERIFY(root.mkpath("source/inner") && root.mkdir("destination"));
        write(root.filePath("source/a.bin"), QByteArray(300000, 'a'));
        write(root.filePath("source/inner/b.bin"), QByteArray(700000, 'b'));
        QVERIFY(QFile::link(root.filePath("source/a.bin"), root.filePath("source/link")));
        FileOperations ops(dir.path(), root.filePath("Trash"));
        auto progress = std::make_shared<CopyProgress>();
        progress->total = FileOperations::measure({root.filePath("source")});
        QCOMPARE(progress->total.load(), qint64(1000000)); // Links are not counted twice.
        QVERIFY(ops.copy(root.filePath("source"), root.filePath("destination"), {}, progress).ok);
        QCOMPARE(progress->done.load(), progress->total.load());
        QCOMPARE(QFileInfo(root.filePath("destination/source/inner/b.bin")).size(), qint64(700000));
    }
    void cancelLeavesNoPublishedPartialCopy()
    {
        QTemporaryDir dir;
        QDir root(dir.path());
        root.mkdir("destination");
        write(root.filePath("source"), QByteArray(1024 * 1024, 'x'));
        FileOperations ops(dir.path(), root.filePath("Trash"));
        auto cancel = std::make_shared<std::atomic_bool>(true);
        QVERIFY(!ops.copy(root.filePath("source"), root.filePath("destination"), cancel).ok);
        QCOMPARE(QDir(root.filePath("destination"))
                     .entryList(QDir::AllEntries | QDir::Hidden | QDir::NoDotAndDotDot)
                     .size(),
                 0);
    }
    void unreadableDirectoryCannotPublishEmptyCopy()
    {
        QTemporaryDir dir;
        QDir root(dir.path());
        root.mkdir("locked");
        root.mkdir("destination");
        write(root.filePath("locked/valuable"), "keep");
        const auto mode = QFileInfo(root.filePath("locked")).permissions();
        QVERIFY(QFile::setPermissions(root.filePath("locked"), {}));
        FileOperations ops(dir.path(), root.filePath("Trash"));
        const auto result = ops.copy(root.filePath("locked"), root.filePath("destination"));
        QVERIFY(QFile::setPermissions(root.filePath("locked"), mode));
        QVERIFY(!result.ok);
        QVERIFY(!QFileInfo(root.filePath("destination/locked")).exists());
        QCOMPARE(QDir(root.filePath("destination"))
                     .entryList(QDir::AllEntries | QDir::Hidden | QDir::NoDotAndDotDot)
                     .size(),
                 0);
    }
    void emptyingTrashRemovesOnlyTrashedItemsWithoutFollowingLinks()
    {
        QTemporaryDir dir;
        QDir root(dir.path());
        QVERIFY(root.mkpath("outside/keep") && root.mkpath("docs/folder/nested"));
        write(root.filePath("outside/keep/precious.txt"), "keep me");
        write(root.filePath("docs/folder/nested/a.txt"), "a");
        write(root.filePath("docs/file.txt"), "b");
        // A trashed folder containing a link to data outside Trash.
        QVERIFY(QFile::link(root.filePath("outside/keep"), root.filePath("docs/folder/link")));
        FileOperations ops(dir.path(), root.filePath("Trash"));
        QVERIFY(ops.trash(root.filePath("docs/folder")).ok);
        QVERIFY(ops.trash(root.filePath("docs/file.txt")).ok);
        const auto items = ops.trashedItems();
        QCOMPARE(items.size(), 2);
        QVERIFY(!ops.removeFromTrash(root.filePath("outside/keep/precious.txt")).ok);
        QVERIFY(!ops.removeFromTrash(root.filePath("Trash/files/folder/nested")).ok);
        for (const auto &item : items)
            QVERIFY2(ops.removeFromTrash(item).ok, qPrintable(item));
        QVERIFY(ops.trashedItems().isEmpty());
        QVERIFY(QDir(root.filePath("Trash/info")).entryList(QDir::Files).isEmpty());
        QFile kept(root.filePath("outside/keep/precious.txt"));
        QVERIFY(kept.open(QIODevice::ReadOnly));
        QCOMPARE(kept.readAll(), QByteArray("keep me"));
        QVERIFY(QFileInfo(root.filePath("Trash/files")).isDir());
    }
    void aFolderOnAnotherDiskGoesToTrashAndBack()
    {
        // A folder on a flash drive (another filesystem than the Trash) failed to go to the Trash with
        // "Can't recursively copy directory" and stayed on the drive (found on Potato, 2026-11-16).
        QTemporaryDir home;
        std::unique_ptr<QTemporaryDir> drive;
        for (const auto &place : {QString("/dev/shm"), QCoreApplication::applicationDirPath()})
        {
            struct stat a {}, b {};
            if (::stat(QFile::encodeName(place).constData(), &a) == 0 &&
                ::stat(QFile::encodeName(home.path()).constData(), &b) == 0 && a.st_dev != b.st_dev)
            {
                drive = std::make_unique<QTemporaryDir>(place + "/file-tests-drive-XXXXXX");
                break;
            }
        }
        if (!drive || !drive->isValid())
            QSKIP("No second filesystem to stand for a flash drive");
        QDir stick(drive->path());
        QVERIFY(stick.mkpath("Photos/2026") && QDir(home.path()).mkpath("keep"));
        write(stick.filePath("Photos/2026/beach.jpg"), "picture");
        write(home.path() + "/keep/precious.txt", "keep me");
        QVERIFY(QFile::link(home.path() + "/keep", stick.filePath("Photos/link")));
        FileOperations ops(home.path(), home.path() + "/Trash", {drive->path()});
        QVERIFY(!FileOperations::sameVolume({stick.filePath("Photos")}, home.path()));
        const auto trashed = ops.trash(stick.filePath("Photos"));
        QVERIFY2(trashed.ok, qPrintable(trashed.error));
        QVERIFY(!QFileInfo::exists(stick.filePath("Photos")));
        QFile copied(trashed.path + "/2026/beach.jpg");
        QVERIFY(copied.open(QIODevice::ReadOnly));
        QCOMPARE(copied.readAll(), QByteArray("picture"));
        QVERIFY(QFileInfo(trashed.path + "/link").isSymLink()); // Copied as a link, never followed.
        QVERIFY(QFileInfo::exists(home.path() + "/keep/precious.txt"));
        QVERIFY(QFileInfo::exists(home.path() + "/Trash/info/Photos.trashinfo"));
        QVERIFY(stick.entryList(QDir::AllEntries | QDir::Hidden | QDir::NoDotAndDotDot).isEmpty());
        // Put Away: back to the drive.
        const auto restored = ops.restore(trashed.path);
        QVERIFY2(restored.ok, qPrintable(restored.error));
        QCOMPARE(restored.path, stick.filePath("Photos"));
        QVERIFY(QFileInfo::exists(stick.filePath("Photos/2026/beach.jpg")));
        QVERIFY(ops.trashedItems().isEmpty());
        QVERIFY(QDir(home.path() + "/Trash/info").entryList(QDir::Files).isEmpty());
    }
    void crossDriveSingleFilePreservesConcurrentEdit_data()
    {
        QTest::addColumn<QString>("operation");
        QTest::addColumn<bool>("published");
        for (const auto *operation : {"trash", "restore", "move"})
            for (const bool published : {false, true})
                QTest::newRow(qPrintable(QString(operation) + (published ? "-published" : "-copying")))
                    << QString(operation) << published;
    }
    void crossDriveSingleFilePreservesConcurrentEdit()
    {
        QFETCH(QString, operation);
        QFETCH(bool, published);
        QTemporaryDir home;
        const auto build = QFileInfo(QFile::symLinkTarget("/proc/self/exe")).absolutePath();
        QTemporaryDir drive(build + "/file-tests-drive-XXXXXX");
        QVERIFY(home.isValid() && drive.isValid());
        if (FileOperations::sameVolume({drive.path()}, home.path()))
            QSKIP("No second writable filesystem for cross-drive single-file checks");
        auto source = drive.filePath("notes.txt");
        write(source, "original version");
        FileOperations ops(home.path(), home.filePath("Trash"), {drive.path()});
        if (operation == "restore")
        {
            const auto initial = ops.trash(source);
            QVERIFY2(initial.ok, qPrintable(initial.error));
            source = initial.path;
        }
        const auto reset = qScopeGuard([] {
            duringMove = {}; mutateDuringMoving.clear(); afterCopy = {}; afterPublish = {}; mutateAfterCopying.clear();
        });
        bool edited = false;
        const auto edit = [&] { write(source, "newer version"); edited = true; };
        if (published)
            afterPublish = edit;
        else
        {
            mutateDuringMoving = mutateAfterCopying = source;
            duringMove = afterCopy = edit;
        }
        const auto result = operation == "restore" ? ops.restore(source) :
                            operation == "move" ? ops.move(source, home.path()) : ops.trash(source);
        QVERIFY(edited);
        QFile latest(source);
        QVERIFY2(latest.open(QIODevice::ReadOnly), "A newer individual source file was deleted after copying");
        QCOMPARE(latest.readAll(), QByteArray("newer version"));
        QVERIFY(!result.ok);
        if (published)
        {
            QFile copy(result.path);
            QVERIFY(copy.open(QIODevice::ReadOnly));
            QCOMPARE(copy.readAll(), QByteArray("original version"));
        }
    }
    void crossDriveSingleFilesAndLinksMoveWithoutFollowingTargets()
    {
        QTemporaryDir home;
        const auto build = QFileInfo(QFile::symLinkTarget("/proc/self/exe")).absolutePath();
        QTemporaryDir drive(build + "/file-tests-drive-XXXXXX");
        QVERIFY(home.isValid() && drive.isValid());
        if (FileOperations::sameVolume({drive.path()}, home.path()))
            QSKIP("No second writable filesystem for cross-drive file/link checks");
        write(drive.filePath("notes.txt"), "contents");
        FileOperations ops(home.path(), home.filePath("Trash"), {drive.path()});
        const auto trash = ops.trash(drive.filePath("notes.txt"));
        QVERIFY2(trash.ok, qPrintable(trash.error));
        const auto restore = ops.restore(trash.path);
        QVERIFY2(restore.ok, qPrintable(restore.error));
        QFile content(restore.path);
        QVERIFY(content.open(QIODevice::ReadOnly));
        QCOMPARE(content.readAll(), QByteArray("contents"));
        QVERIFY(QFile::link(drive.filePath("notes.txt"), drive.filePath("alias")));
        const auto moved = ops.move(drive.filePath("alias"), home.path());
        QVERIFY2(moved.ok, qPrintable(moved.error));
        QVERIFY(QFileInfo(moved.path).isSymLink());
        QCOMPARE(QFileInfo(moved.path).symLinkTarget(), drive.filePath("notes.txt"));
        QVERIFY(QFileInfo(restore.path).isFile());
        QVERIFY(!QFileInfo(drive.filePath("alias")).isSymLink());
    }
    void crossDriveTrashCanMoveHardLinkedFiles()
    {
        QTemporaryDir home;
        const auto build = QFileInfo(QFile::symLinkTarget("/proc/self/exe")).absolutePath();
        QTemporaryDir drive(build + "/file-tests-drive-XXXXXX");
        QVERIFY(home.isValid() && drive.isValid());
        if (FileOperations::sameVolume({drive.path()}, home.path()))
            QSKIP("No second writable filesystem for cross-drive hard-link checks");
        const auto source = drive.filePath("Documents");
        QVERIFY(QDir().mkdir(source));
        write(source + "/first.txt", "shared contents");
        QVERIFY(::link(QFile::encodeName(source + "/first.txt").constData(),
                       QFile::encodeName(source + "/second.txt").constData()) == 0);
        QVERIFY(::link(QFile::encodeName(source + "/first.txt").constData(),
                       QFile::encodeName(drive.filePath("outside.txt")).constData()) == 0);
        FileOperations ops(home.path(), home.filePath("Trash"), {drive.path()});
        const auto result = ops.trash(source);
        QVERIFY2(result.ok, qPrintable(result.error));
        QVERIFY(!QFileInfo::exists(source));
        for (const auto &path : {result.path + "/first.txt", result.path + "/second.txt", drive.filePath("outside.txt")})
        {
            QFile content(path);
            QVERIFY(content.open(QIODevice::ReadOnly));
            QCOMPARE(content.readAll(), QByteArray("shared contents"));
        }
        const auto restored = ops.restore(result.path);
        QVERIFY2(restored.ok, qPrintable(restored.error));
        QVERIFY(QFileInfo::exists(source + "/first.txt") && QFileInfo::exists(source + "/second.txt"));
        QVERIFY(QFileInfo::exists(drive.filePath("outside.txt")));
    }
    void crossDriveTrashPreservesSourceChanges_data()
    {
        QTest::addColumn<QString>("change");
        QTest::addColumn<bool>("restore");
        for (const bool restore : {false, true})
            for (const auto &change : {QString("during"), QString("edit"), QString("same-time"), QString("add"), QString("replace")})
                QTest::newRow(qPrintable((restore ? QString("restore-") : QString("trash-")) + change)) << change << restore;
    }
    void crossDriveTrashPreservesSourceChanges()
    {
        QFETCH(QString, change);
        QFETCH(bool, restore);
        QTemporaryDir home;
        const auto build = QFileInfo(QFile::symLinkTarget("/proc/self/exe")).absolutePath();
        QTemporaryDir drive(build + "/file-tests-drive-XXXXXX");
        QVERIFY(home.isValid() && drive.isValid());
        if (FileOperations::sameVolume({drive.path()}, home.path()))
            QSKIP("No second writable filesystem for cross-drive mutation checks");
        auto source = drive.filePath("Documents");
        QVERIFY(QDir().mkdir(source));
        write(source + "/notes.txt", "original version");
        FileOperations ops(home.path(), home.filePath("Trash"), {drive.path()});
        if (restore)
        {
            const auto initial = ops.trash(source);
            QVERIFY2(initial.ok, qPrintable(initial.error));
            source = initial.path;
        }
        const auto reset = qScopeGuard([] { afterCopy = {}; afterPublish = {}; mutateAfterCopying.clear(); });
        bool edited = false;
        const auto edit = [&] {
            if (change == "replace")
            {
                QVERIFY(QDir().rename(source, QFileInfo(source).dir().filePath("Original Documents")));
                QVERIFY(QDir().mkdir(source));
                write(source + "/notes.txt", "replacement folder");
            }
            else if (change == "add")
                write(source + "/new.txt", "new file");
            else if (change == "same-time")
            {
                const auto path = QFile::encodeName(source + "/notes.txt");
                struct stat before {};
                QVERIFY(::stat(path.constData(), &before) == 0);
                write(source + "/notes.txt", "modified version");
                const struct timespec times[] = {before.st_atim, before.st_mtim};
                QVERIFY(utimensat(AT_FDCWD, path.constData(), times, 0) == 0);
            }
            else
                write(source + "/notes.txt", "newer version");
            edited = true;
        };
        if (change == "during")
        {
            mutateAfterCopying = source + "/notes.txt";
            afterCopy = edit;
        }
        else
            afterPublish = edit;
        const auto result = restore ? ops.restore(source) : ops.trash(source);
        QVERIFY(edited);
        const auto kept = source + (change == "add" ? "/new.txt" : "/notes.txt");
        QFile latest(kept);
        QVERIFY2(latest.open(QIODevice::ReadOnly), "New source data was removed after copying an older version");
        QCOMPARE(latest.readAll(), change == "add" ? QByteArray("new file") :
                                 change == "replace" ? QByteArray("replacement folder") :
                                 change == "same-time" ? QByteArray("modified version") : QByteArray("newer version"));
        QVERIFY2(!result.ok, "A changing source must not be reported as a fully completed move");
        if (change != "during")
        {
            QVERIFY(!result.path.isEmpty());
            QFile copied(result.path + "/notes.txt");
            QVERIFY(copied.open(QIODevice::ReadOnly));
            QCOMPARE(copied.readAll(), QByteArray("original version"));
            QVERIFY(QFileInfo::exists(home.filePath("Trash/info/Documents.trashinfo")));
        }
    }
    void trashCannotConsumeItself()
    {
        QTemporaryDir dir;
        QDir root(dir.path());
        root.mkpath("Trash/files");
        write(root.filePath("Trash/files/keep"), "keep");
        FileOperations ops(dir.path(), root.filePath("Trash"));
        QVERIFY(!ops.trash(root.filePath("Trash")).ok);
        QVERIFY(!ops.trash(root.filePath("Trash/files/keep")).ok);
        QVERIFY(QFileInfo(root.filePath("Trash/files/keep")).exists());
    }
    void fileSymlinkCannotOpenOutsideWorkspace()
    {
        QTemporaryDir root;
        QTemporaryDir outside;
        write(outside.path() + "/private", "keep");
        QVERIFY(QFile::link(outside.path() + "/private", root.path() + "/link"));
        FileOperations ops(root.path(), root.path() + "/Trash");
        QVERIFY(!ops.readableFile(root.path() + "/link"));
        // Copying the link itself is permitted and must not copy its target.
        QDir(root.path()).mkdir("destination");
        const auto copied = ops.copy(root.path() + "/link", root.path() + "/destination");
        QVERIFY2(copied.ok, qPrintable(copied.error));
        QVERIFY(QFileInfo(copied.path).isSymLink());
    }
};
QTEST_APPLESS_MAIN(FileTests)
#include "file_tests.moc"
