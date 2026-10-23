#pragma once
#include <QWidget>

class QLabel;
class QPlainTextEdit;
class QScrollArea;
class QStackedWidget;

// The Finder's Edit > Show Clipboard window, as in Mac OS 9.2.2 (checked in psn_tch's 9.2.2 VM):
// "Clipboard contents: text" (or "picture", "none") in a grey strip above the contents, in a
// read-only pane. One per Finder; it follows the clipboard while open.
class ClipboardWindow final : public QWidget
{
  public:
    explicit ClipboardWindow(QWidget *parent = nullptr);
    // Opens (or raises) the one Clipboard window.
    static ClipboardWindow *show(QWidget *parent);
    QString kind() const { return m_kind; }

  private:
    void refresh();
    QLabel *m_description;
    QStackedWidget *m_pages;
    QPlainTextEdit *m_text;
    QScrollArea *m_pictureArea;
    QLabel *m_picture;
    QString m_kind = "none";
};
