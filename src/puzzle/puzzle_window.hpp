#pragma once
#include "sliding_puzzle.hpp"
#include <QImage>
#include <QMainWindow>
#include <array>
class QAction;
class QPushButton;
class QLabel;

class PuzzleWindow final : public QMainWindow
{
    Q_OBJECT
  public:
    explicit PuzzleWindow(SlidingPuzzle puzzle, QWidget *parent = nullptr);
    void newPuzzle();
    // System 7's Puzzle (Lily Hewitt, 2026-11-12: her grandma's Mac IIsi showed the Apple, not numbers):
    // the tiles are pieces of a picture, the user's own rainbow Apple; Edit ▸ Clear switches between
    // the picture and numbers. Without a picture, numbers.
    void setPicture(const QImage &picture);
    bool showingPicture() const { return !m_pieces.isEmpty() && m_showPicture; }
    void clear();
    // The rainbow Apple that Asset Importer took from the user's Mac OS 9 System file, or none.
    static QImage personalApple();

  protected:
    void keyPressEvent(QKeyEvent *event) override;

  private:
    // The blank square's offset for an arrow key, or 0 for any other key.
    static int arrowOffset(int key);
    void move(int index);
    void refresh();
    SlidingPuzzle m_puzzle;
    std::array<QPushButton *, 16> m_buttons;
    QLabel *m_status;
    // The picture cut into 15 pieces (piece n-1 belongs to tile n), each the inside of a tile.
    QList<QPixmap> m_pieces;
    bool m_showPicture = true;
    QAction *m_clear;
};
