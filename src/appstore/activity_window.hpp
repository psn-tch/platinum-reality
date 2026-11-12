#pragma once
#include <QWidget>

class QLabel;
class QPlainTextEdit;
class QProgressBar;

// Software ▸ Show Activity (Lily Hewitt, 2026-11-11): what the App Store is doing, line by line, as
// pacman, yay and makepkg write it, like a small Terminal window. It only shows; it never asks or
// types anything. The log is kept while the App Store is open, the newest 5000 lines.
class ActivityWindow final : public QWidget
{
    Q_OBJECT
  public:
    explicit ActivityWindow(QWidget *parent = nullptr);
    void begin(const QString &doing);
    void line(const QString &text);
    void progress(int step, int steps);
    void finish(bool ok, const QString &error, bool cancelled);
    QString text() const;

  private:
    void heading(const QString &text);
    QLabel *m_heading;
    QProgressBar *m_progress;
    QPlainTextEdit *m_log;
    QString m_doing;
};
