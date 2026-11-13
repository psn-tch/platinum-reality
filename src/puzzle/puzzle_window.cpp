#include "puzzle_window.hpp"
#include "finder/platinum_style.hpp"
#include <QApplication>
#include <QDir>
#include <QFileInfo>
#include <QFrame>
#include <QKeyEvent>
#include <QLabel>
#include <QMenuBar>
#include <QPainter>
#include <QPushButton>
#include <QRandomGenerator>
#include <QShortcut>
#include <QStyleOptionButton>
#include <algorithm>

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
    // As System 7's Puzzle desk accessory: only Clear does anything, switching picture and numbers.
    auto *editMenu = menuBar()->addMenu("Edit");
    for (const auto &[text, key] : {std::pair{"Undo", QKeySequence::Undo}, std::pair{"", QKeySequence::UnknownKey},
                                    std::pair{"Cut", QKeySequence::Cut}, std::pair{"Copy", QKeySequence::Copy},
                                    std::pair{"Paste", QKeySequence::Paste}})
    {
        if (!*text)
        {
            editMenu->addSeparator();
            continue;
        }
        auto *action = editMenu->addAction(text);
        setClassicShortcuts(action, key);
        action->setEnabled(false);
    }
    m_clear = editMenu->addAction("Clear", this, &PuzzleWindow::clear);
    m_clear->setObjectName("puzzle-clear");
    m_clear->setEnabled(false);
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
QImage PuzzleWindow::personalApple()
{
    auto folder = qEnvironmentVariable("PLATINUM_PERSONAL_ASSETS");
    if (folder.isEmpty())
    {
        auto data = qEnvironmentVariable("XDG_DATA_HOME");
        if (!QDir::isAbsolutePath(data))
            data = QDir::homePath() + "/.local/share";
        folder = data + "/platinum-reality/assets";
    }
    for (const auto &name : {"apple-menu.png", "apple-menu-16.png"})
    {
        const QFileInfo file(folder + "/icons/" + name);
        if (file.isFile() && file.size() < 1024 * 1024)
            if (QImage image(file.filePath()); !image.isNull())
                return image;
    }
    return {};
}

void PuzzleWindow::setPicture(const QImage &picture)
{
    m_pieces.clear();
    if (!picture.isNull())
    {
        // The board is the 4x4 tiles' insides (where the style puts a button's label) side by side.
        // The picture's visible part is enlarged by a whole number, square pixels as a Mac drew it,
        // and centred on white, as System 7's was.
        QStyleOptionButton option;
        option.initFrom(m_buttons[0]);
        option.rect = m_buttons[0]->rect();
        const auto label = m_buttons[0]->style()->subElementRect(QStyle::SE_PushButtonContents, &option, m_buttons[0]);
        const int inside = std::min(label.width(), label.height()), board = inside * 4;
        QImage visible = picture.convertToFormat(QImage::Format_ARGB32);
        QRect bounds;
        for (int y = 0; y < visible.height(); ++y)
            for (int x = 0; x < visible.width(); ++x)
                if (qAlpha(visible.pixel(x, y)) > 0)
                    bounds |= QRect(x, y, 1, 1);
        if (bounds.isValid())
            visible = visible.copy(bounds);
        const int scale = std::max(1, std::min(board / visible.width(), board / visible.height()));
        QPixmap whole(board, board);
        whole.fill(Qt::white);
        QPainter painter(&whole);
        const QSize size = visible.size() * scale;
        painter.drawImage(QRect(QPoint((board - size.width()) / 2, (board - size.height()) / 2), size), visible);
        painter.end();
        for (int n = 0; n < 15; ++n)
            m_pieces.append(whole.copy((n % 4) * inside, (n / 4) * inside, inside, inside));
        for (auto *button : m_buttons)
            button->setIconSize(QSize(inside, inside));
    }
    m_clear->setEnabled(!m_pieces.isEmpty());
    refresh();
}

void PuzzleWindow::clear()
{
    m_showPicture = !m_showPicture;
    refresh();
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
        const bool picture = number && showingPicture();
        m_buttons[i]->setText(number && !picture ? QString::number(number) : QString());
        m_buttons[i]->setIcon(picture ? QIcon(m_pieces[number - 1]) : QIcon());
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
