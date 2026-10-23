#pragma once
#include "sliding_puzzle.hpp"
#include <QMainWindow>
#include <array>
class QPushButton;
class QLabel;

class PuzzleWindow final : public QMainWindow
{
    Q_OBJECT
  public:
    explicit PuzzleWindow(SlidingPuzzle puzzle, QWidget *parent = nullptr);
    void newPuzzle();

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
};
