#include "finder/file_operations.hpp"
#include <QTemporaryDir>
#include <QtTest>

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
