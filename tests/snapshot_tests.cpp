#include "appearance/interface_font.hpp"
#include "finder/platinum_style.hpp"
#include "snapshot/snapshot_window.hpp"
#include <QDateTime>
#include <QFile>
#include <QSet>
#include <QImageReader>
#include <QLabel>
#include <QMediaDevices>
#include <QPushButton>
#include <QStackedWidget>
#include <QTemporaryDir>
#include <QVideoFrame>
#include <QVideoSink>
#include <QVideoWidget>
#include <QtTest>

// Snapshot (Lily Hewitt, 2026-11-12: "a super simple photo booth ... to take basic photos with webcam
// and test its working"). Capturing needs a real camera; these check what doesn't.
class SnapshotTests : public QObject
{
    Q_OBJECT
  private slots:
    void initTestCase()
    {
        qApp->setStyle(new PlatinumStyle);
        qApp->setFont(classicInterfaceFont());
    }
    void withoutACameraItSaysWhyAndCannotTakeAPicture()
    {
        if (!QMediaDevices::videoInputs().isEmpty())
            QSKIP("This computer has a camera.");
        QTemporaryDir root;
        SnapshotWindow window(QDir(root.filePath("Pictures/Snapshot")));
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        auto *message = window.findChild<QLabel *>("snapshot-message");
        QVERIFY(message && message->isVisible());
        QVERIFY(message->text().contains("No camera is available."));
        QVERIFY(message->text().contains("Camera switch is Off"));
        QVERIFY(!window.findChild<QPushButton *>("snapshot-take")->isEnabled());
        window.takePicture(); // Nothing happens, nothing is written.
        QVERIFY(!QDir(root.filePath("Pictures")).exists());
        QDir().mkpath(QCoreApplication::applicationDirPath() + "/artifacts");
        QVERIFY(window.grab().save(QCoreApplication::applicationDirPath() + "/artifacts/snapshot-no-camera.png"));
    }
    void picturesAreSavedAsJpegsNamedForWhenTheyWereTaken()
    {
        QTemporaryDir root;
        SnapshotWindow window(QDir(root.filePath("Pictures/Snapshot")));
        QSignalSpy saved(&window, &SnapshotWindow::pictureSaved);
        QImage frame(640, 480, QImage::Format_RGB32);
        frame.fill(QColor("#3366cc"));
        const auto before = QDateTime::currentDateTime();
        const auto first = window.savePicture(frame);
        QVERIFY(!first.isEmpty());
        QCOMPARE(saved.count(), 1);
        QCOMPARE(window.lastPicture(), first);
        QVERIFY(QFileInfo(first).fileName().startsWith("Snapshot " + before.toString("yyyy-MM-dd") + " at "));
        QVERIFY(first.endsWith(".jpg"));
        QImageReader reader(first);
        QCOMPARE(reader.format(), QByteArray("jpeg"));
        QCOMPARE(reader.size(), QSize(640, 480));
        // Two in the same second never replace each other.
        const auto second = window.savePicture(frame);
        const auto third = window.savePicture(frame);
        QVERIFY(!second.isEmpty() && !third.isEmpty());
        QCOMPARE(QSet<QString>({first, second, third}).size(), 3);
        QCOMPARE(QDir(root.filePath("Pictures/Snapshot")).entryList(QDir::Files).size(), 3);
        QVERIFY(!window.findChild<QLabel *>("snapshot-thumbnail")->pixmap().isNull());
        // A frame that is no picture saves nothing.
        QVERIFY(window.savePicture(QImage()).isEmpty());
        QCOMPARE(saved.count(), 3);
    }
    // With a camera (a real one, or v4l2loopback in the test VM): Take Picture saves what it saw.
    void withACameraTakePictureSavesWhatItSaw()
    {
        if (QMediaDevices::videoInputs().isEmpty())
            QSKIP("No camera on this computer.");
        QTemporaryDir root;
        SnapshotWindow window(QDir(root.filePath("Pictures/Snapshot")));
        QSignalSpy saved(&window, &SnapshotWindow::pictureSaved);
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        auto *take = window.findChild<QPushButton *>("snapshot-take");
        QTRY_VERIFY_WITH_TIMEOUT(take->isEnabled(), 15000); // The camera is ready for a picture.
        QVERIFY(window.findChild<QStackedWidget *>()->currentWidget()->objectName() == "snapshot-preview");
        // The live picture: the preview is shown frames from the camera.
        auto *preview = window.findChild<QVideoWidget *>("snapshot-preview");
        QVERIFY(preview && preview->isVisible());
        QTRY_VERIFY_WITH_TIMEOUT(preview->videoSink()->videoFrame().isValid(), 15000);
        const auto shown = preview->videoSink()->videoFrame().toImage();
        QVERIFY(shown.width() >= 160 && shown.height() >= 120);
        QTest::qWait(1500); // A few frames, so the picture isn't the first black one.
        take->click();
        QTRY_COMPARE_WITH_TIMEOUT(saved.count(), 1, 15000);
        const QImage picture(window.lastPicture());
        QVERIFY(!picture.isNull());
        QVERIFY(picture.width() >= 160 && picture.height() >= 120);
        // Not one flat colour: the camera's picture, not an empty frame.
        QSet<QRgb> colours;
        for (int y = 0; y < picture.height(); y += picture.height() / 16)
            for (int x = 0; x < picture.width(); x += picture.width() / 16)
                colours.insert(picture.pixel(x, y) & 0xf0f0f0);
        QVERIFY2(colours.size() > 4, qPrintable(QString("only %1 colours").arg(colours.size())));
        qInfo() << "Picture" << window.lastPicture() << picture.size() << colours.size() << "colours";
        QDir().mkpath(QCoreApplication::applicationDirPath() + "/artifacts");
        QFile::copy(window.lastPicture(), QCoreApplication::applicationDirPath() + "/artifacts/snapshot-camera-picture.jpg");
        window.grab().save(QCoreApplication::applicationDirPath() + "/artifacts/snapshot-camera-window.png");
    }
    void namesSortByTime()
    {
        const QDateTime when(QDate(2026, 11, 12), QTime(21, 5, 9));
        QCOMPARE(SnapshotWindow::nameFor(when), QString("Snapshot 2026-11-12 at 21.05.09.jpg"));
    }
};

QTEST_MAIN(SnapshotTests)
#include "snapshot_tests.moc"
