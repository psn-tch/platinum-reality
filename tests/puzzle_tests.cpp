#include "appearance/interface_font.hpp"
#include "finder/platinum_style.hpp"
#include "puzzle/puzzle_window.hpp"
#include "puzzle/sliding_puzzle.hpp"
#include <QKeyEvent>
#include <QDir>
#include <QLabel>
#include <QPushButton>
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
