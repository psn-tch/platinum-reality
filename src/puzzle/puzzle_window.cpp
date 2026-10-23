#include "puzzle_window.hpp"
#include "finder/platinum_style.hpp"
#include <QApplication>
#include <QFrame>
#include <QKeyEvent>
#include <QLabel>
#include <QMenuBar>
#include <QPushButton>
#include <QRandomGenerator>
#include <QShortcut>

PuzzleWindow::PuzzleWindow(SlidingPuzzle puzzle, QWidget *parent)
    : QMainWindow(parent), m_puzzle(std::move(puzzle))
{
    auto *body = new QWidget;
    body->setFixedSize(206, 240);
    setCentralWidget(body);
    auto *well = new QFrame(body);
    well->setGeometry(14, 10, 178, 178);
    well->setFrameStyle(QFrame::Panel | QFrame::Sunken);
    well->setAutoFillBackground(true);
    auto palette = well->palette();
    palette.setColor(QPalette::Window, QColor("#999999"));
    well->setPalette(palette);
    for (int i = 0; i < 16; ++i)
    {
        auto *button = new QPushButton(body);
        button->setObjectName("puzzle-tile-" + QString::number(i));
        button->setGeometry(16 + (i % 4) * 44, 12 + (i / 4) * 44, 42, 42);
        button->setFocusPolicy(Qt::NoFocus);
        connect(button, &QPushButton::clicked, this, [this, i] { move(i); });
        m_buttons[i] = button;
    }
    auto *shuffle = new QPushButton("Shuffle", body);
    shuffle->setObjectName("puzzle-shuffle");
    shuffle->setGeometry(14, 202, 86, 26);
    // A focused button would turn arrow keys into focus navigation.
    shuffle->setFocusPolicy(Qt::NoFocus);
    connect(shuffle, &QPushButton::clicked, this, &PuzzleWindow::newPuzzle);
    m_status = new QLabel(body);
    m_status->setObjectName("puzzle-status");
    m_status->setGeometry(108, 202, 88, 26);
    auto *file = menuBar()->addMenu("File");
    auto *fresh = file->addAction("New Puzzle");
    setClassicShortcuts(fresh, QKeySequence::New);
    connect(fresh, &QAction::triggered, this, &PuzzleWindow::newPuzzle);
    auto *quit = file->addAction("Quit Puzzle");
    setClassicShortcuts(quit, QKeySequence::Quit);
    connect(quit, &QAction::triggered, this, &QWidget::close);
    // These are game controls even when a button has keyboard focus. A window
    // keyPressEvent alone loses arrows to QPushButton's focus navigation.
    for (const auto key : {Qt::Key_Left, Qt::Key_Right, Qt::Key_Up, Qt::Key_Down})
    {
        auto *shortcut = new QShortcut(QKeySequence(key), this);
        connect(shortcut, &QShortcut::activated, this,
                [this, key] { move(m_puzzle.blank() + arrowOffset(key)); });
    }
    setFocusPolicy(Qt::StrongFocus);
    refresh();
    adjustSize();
    setFixedSize(sizeHint());
}
int PuzzleWindow::arrowOffset(int key)
{
    switch (key)
    {
    case Qt::Key_Left: return -1;
    case Qt::Key_Right: return 1;
    case Qt::Key_Up: return -4;
    case Qt::Key_Down: return 4;
    default: return 0;
    }
}
// Window shortcuts only match once the window is active. Under xdg-shell that follows the
// compositor's "activated" configure, which can arrive just after keyboard focus; arrows
// pressed in between reach the window here instead. A matched shortcut consumes the key,
// so a press is never handled twice.
void PuzzleWindow::keyPressEvent(QKeyEvent *event)
{
    if (const auto offset = arrowOffset(event->key()); offset && event->modifiers() == Qt::NoModifier)
    {
        move(m_puzzle.blank() + offset);
        event->accept();
        return;
    }
    QMainWindow::keyPressEvent(event);
}
void PuzzleWindow::newPuzzle()
{
    m_puzzle.shuffle(QRandomGenerator::global()->generate());
    refresh();
}
void PuzzleWindow::move(int index)
{
    if (m_puzzle.move(index))
        refresh();
}
void PuzzleWindow::refresh()
{
    for (int i = 0; i < 16; ++i)
    {
        const auto number = m_puzzle.tiles()[i];
        m_buttons[i]->setText(number ? QString::number(number) : QString());
        m_buttons[i]->setVisible(number != 0);
        m_buttons[i]->setAccessibleName(number ? QString("Tile %1").arg(number) : "Empty space");
    }
    m_status->setText(m_puzzle.solved() ? "You did it!"
                                        : QString("Moves: %1").arg(m_puzzle.moves()));
    setWindowTitle(m_puzzle.solved()  ? "Puzzle — Solved!"
                   : m_puzzle.moves() ? QString("Puzzle — %1 move%2")
                                            .arg(m_puzzle.moves())
                                            .arg(m_puzzle.moves() == 1 ? "" : "s")
                                      : "Puzzle");
}
