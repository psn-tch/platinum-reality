#pragma once
#include <QMainWindow>
class QTextEdit;
class EditorWindow final : public QMainWindow
{
    Q_OBJECT
  public:
    explicit EditorWindow(QString file = {}, QWidget *parent = nullptr);
    bool save();

  protected:
    void closeEvent(QCloseEvent *) override;

  private:
    void updateTitle();
    QString m_path;
    QTextEdit *m_text;
};
