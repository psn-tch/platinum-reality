#include "appearance/interface_font.hpp"
#include "finder/platinum_style.hpp"
#include "puzzle/puzzle_window.hpp"
#include "puzzle/sliding_puzzle.hpp"
#include <QKeyEvent>
#include <QDir>
#include <QLabel>
#include <QPushButton>
#include <QAction>
#include <QTemporaryDir>
#include <QtTest>
#include <algorithm>

class PuzzleTests : public QObject
{
    Q_OBJECT
  private slots:
    void initTestCase()
    {
        qApp->setStyle(new PlatinumStyle);
        qApp->setFont(classicInterfaceFont());
    }
    void onlyAdjacentTilesMoveAndTheGoalCanBeRestored()
    {
        SlidingPuzzle game;
        QVERIFY(game.solved());
        const auto start = game.tiles();
        QVERIFY(!game.move(-1));
        QVERIFY(!game.move(16));
        QVERIFY(!game.move(0));
        QVERIFY(!game.move(15));
        QCOMPARE(game.tiles(), start);
        QCOMPARE(game.moves(), 0);
        QVERIFY(game.move(14));
        QVERIFY(!game.solved());
        QVERIFY(!game.move(3)); // Index arithmetic must not wrap rows.
        QVERIFY(game.move(15));
        QVERIFY(game.solved());
        QCOMPARE(game.moves(), 2);
    }
    void shuffledBoardsAreReachableUnsolvedAndContainEveryTile()
    {
        for (unsigned seed = 0; seed < 100; ++seed)
        {
            SlidingPuzzle game;
            game.shuffle(seed);
            QVERIFY(!game.solved());
            QCOMPARE(game.moves(), 0);
            auto values = game.tiles();
            std::sort(values.begin(), values.end());
            for (int i = 0; i < 16; ++i)
                QCOMPARE(values[i], i);
            int inversions = 0;
            for (int i = 0; i < 16; ++i)
                for (int j = i + 1; j < 16; ++j)
                    if (game.tiles()[i] && game.tiles()[j] && game.tiles()[i] > game.tiles()[j])
                        ++inversions;
            QCOMPARE((inversions + 4 - game.blank() / 4) % 2, 1);
        }
    }
    // Lily Hewitt, 2026-11-12: on her grandma's Mac IIsi (System 7) the Puzzle showed the Apple logo, not
    // numbers. The tiles are pieces of the user's own rainbow Apple, enlarged with square pixels;
    // Edit ▸ Clear switches between the picture and numbers.
    void thePictureIsCutIntoTheTilesAndClearSwitchesToNumbers()
    {
        // A picture whose every pixel differs, so each piece shows where it came from.
        QImage picture(16, 16, QImage::Format_RGB32);
        for (int y = 0; y < 16; ++y)
            for (int x = 0; x < 16; ++x)
                picture.setPixel(x, y, qRgb(x * 16, y * 16, 128));
        SlidingPuzzle game;
        QVERIFY(game.move(14));
        PuzzleWindow window(game);
        auto *clear = window.findChild<QAction *>("puzzle-clear");
        QVERIFY(clear);
        QVERIFY(!clear->isEnabled()); // No picture: numbers, and nothing to clear.
        QCOMPARE(window.findChild<QPushButton *>("puzzle-tile-0")->text(), QString("1"));
        window.setPicture(picture);
        QVERIFY(clear->isEnabled());
        QVERIFY(window.showingPicture());
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        const int inside = window.findChild<QPushButton *>("puzzle-tile-0")->iconSize().width();
        QVERIFY(inside > 20 && inside <= 42);
        const int scale = inside * 4 / 16, margin = (inside * 4 - 16 * scale) / 2;
        for (int i = 0; i < 16; ++i)
        {
            auto *tile = window.findChild<QPushButton *>("puzzle-tile-" + QString::number(i));
            const int number = window.findChild<QPushButton *>("puzzle-tile-" + QString::number(i))->accessibleName() == "Empty space"
                                   ? 0 : game.tiles()[i];
            if (!number)
                continue;
            QVERIFY(tile->text().isEmpty());
            const auto piece = tile->icon().pixmap(inside, inside).toImage();
            QCOMPARE(piece.size(), QSize(inside, inside));
            // The middle of tile n's piece is the picture's pixel there, at whole-number scale.
            const int bx = (number - 1) % 4 * inside + inside / 2, by = (number - 1) / 4 * inside + inside / 2;
            const int px = (bx - margin) / scale, py = (by - margin) / scale;
            QCOMPARE(piece.pixel(inside / 2, inside / 2), picture.pixel(px, py));
        }
        QDir().mkpath(QCoreApplication::applicationDirPath() + "/artifacts");
        QVERIFY(window.grab().save(QCoreApplication::applicationDirPath() + "/artifacts/puzzle-picture.png"));
        // A move keeps the pieces with their tiles.
        auto *moved = window.findChild<QPushButton *>("puzzle-tile-15");
        const auto fifteen = moved->icon().pixmap(inside, inside).toImage();
        QTest::mouseClick(moved, Qt::LeftButton);
        QCOMPARE(window.findChild<QPushButton *>("puzzle-tile-14")->icon().pixmap(inside, inside).toImage(), fifteen);
        QVERIFY(!moved->isVisible());
        // Clear: numbers; Clear again: the picture.
        clear->trigger();
        QVERIFY(!window.showingPicture());
        QCOMPARE(window.findChild<QPushButton *>("puzzle-tile-0")->text(), QString("1"));
        QVERIFY(window.findChild<QPushButton *>("puzzle-tile-0")->icon().isNull());
        clear->trigger();
        QVERIFY(window.showingPicture());
        QVERIFY(window.findChild<QPushButton *>("puzzle-tile-0")->text().isEmpty());
        // A picture that can't be read leaves numbers.
        window.setPicture(QImage());
        QVERIFY(!clear->isEnabled());
        QCOMPARE(window.findChild<QPushButton *>("puzzle-tile-0")->text(), QString("1"));
    }
    void thePersonalAppleComesFromTheImportedAssets()
    {
        QTemporaryDir assets;
        QVERIFY(assets.isValid());
        const auto previous = qgetenv("PLATINUM_PERSONAL_ASSETS");
        const auto restore = qScopeGuard([&] { qputenv("PLATINUM_PERSONAL_ASSETS", previous); });
        qputenv("PLATINUM_PERSONAL_ASSETS", assets.path().toUtf8());
        QVERIFY(PuzzleWindow::personalApple().isNull());
        QDir(assets.path()).mkpath("icons");
        QImage apple(16, 16, QImage::Format_ARGB32);
        apple.fill(Qt::red);
        QVERIFY(apple.save(assets.filePath("icons/apple-menu.png")));
        QCOMPARE(PuzzleWindow::personalApple().size(), QSize(16, 16));
    }
    void actualTileClicksWinAndShuffleStartsAnotherGame()
    {
        SlidingPuzzle game;
        QVERIFY(game.move(14));
        PuzzleWindow window(game);
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        auto *wrong = window.findChild<QPushButton *>("puzzle-tile-0");
        auto *finish = window.findChild<QPushButton *>("puzzle-tile-15");
        auto *status = window.findChild<QLabel *>("puzzle-status");
        QVERIFY(wrong && finish && status);
        QTest::mouseClick(wrong, Qt::LeftButton);
        QCOMPARE(status->text(), QString("Moves: 1"));
        QTest::mouseClick(finish, Qt::LeftButton);
        QCOMPARE(window.windowTitle(), QString("Puzzle — Solved!"));
        QCOMPARE(status->text(), QString("You did it!"));
        QVERIFY(!finish->isVisible());
        QDir().mkpath(QCoreApplication::applicationDirPath() + "/artifacts");
        QVERIFY(window.grab().save(QCoreApplication::applicationDirPath() +
                                   "/artifacts/puzzle-solved.png"));
        auto *shuffle = window.findChild<QPushButton *>("puzzle-shuffle");
        QVERIFY(shuffle);
        QTest::mouseClick(shuffle, Qt::LeftButton);
        QCOMPARE(window.windowTitle(), QString("Puzzle"));
        QCOMPARE(status->text(), QString("Moves: 0"));
        QVERIFY(
            window.grab().save(QCoreApplication::applicationDirPath() + "/artifacts/puzzle.png"));
        QCOMPARE(window.minimumSize(), window.maximumSize());
    }
    void arrowKeysMoveTheSpace()
    {
        SlidingPuzzle game;
        QVERIFY(game.move(14));
        PuzzleWindow window(game);
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        auto *shuffle = window.findChild<QPushButton *>("puzzle-shuffle");
        QVERIFY(shuffle);
        shuffle->setFocus();
        QCOMPARE(window.focusWidget(), shuffle);
        // Use the platform window so Qt routes the event through the actual
        // focused widget, rather than delivering directly to PuzzleWindow.
        QTest::keyClick(window.windowHandle(), Qt::Key_Right);
        QCOMPARE(window.windowTitle(), QString("Puzzle — Solved!"));
        QTest::keyClick(window.windowHandle(), Qt::Key_Right); // No row wrapping.
        QCOMPARE(window.windowTitle(), QString("Puzzle — Solved!"));
        QTest::keyClick(window.windowHandle(), Qt::Key_Up);
        QCOMPARE(window.windowTitle(), QString("Puzzle — 3 moves"));
        QTest::keyClick(window.windowHandle(), Qt::Key_Down);
        QCOMPARE(window.windowTitle(), QString("Puzzle — Solved!"));
        QCOMPARE(window.focusWidget(), shuffle);
        QTest::keyClick(window.windowHandle(), Qt::Key_Space);
        QCOMPARE(window.windowTitle(), QString("Puzzle"));
    }
    void arrowsReachAWindowThatIsNotYetActive()
    {
        // Under xdg-shell the window becomes active only after the compositor's "activated"
        // configure; window shortcuts don't match before then, so the window handles arrows.
        SlidingPuzzle game;
        QVERIFY(game.move(14));
        PuzzleWindow window(game);
        QVERIFY(!window.isActiveWindow());
        const auto press = [&](Qt::Key key, Qt::KeyboardModifiers modifiers = Qt::NoModifier)
        {
            QKeyEvent event(QEvent::KeyPress, key, modifiers);
            QApplication::sendEvent(&window, &event);
            return event.isAccepted();
        };
        QVERIFY(press(Qt::Key_Right));
        QCOMPARE(window.windowTitle(), QString("Puzzle — Solved!"));
        QVERIFY(press(Qt::Key_Up));
        QCOMPARE(window.windowTitle(), QString("Puzzle — 3 moves"));
        press(Qt::Key_Down, Qt::ControlModifier); // Not a game control.
        QCOMPARE(window.windowTitle(), QString("Puzzle — 3 moves"));
        QCOMPARE(window.findChild<QPushButton *>("puzzle-shuffle")->focusPolicy(), Qt::NoFocus);
    }
};
QTEST_MAIN(PuzzleTests)
#include "puzzle_tests.moc"
