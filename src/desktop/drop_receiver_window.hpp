#pragma once
#include <QBackingStore>
#include <QWindow>

// Private client endpoint for the compositor-owned desktop. Its surface is
// never placed in the scene; filesystem jobs remain in the desktop process.
class DesktopDropReceiver final : public QWindow
{
    Q_OBJECT
  public:
    DesktopDropReceiver();
  signals:
    void filesDropped(QStringList paths, QString token);

  protected:
    bool event(QEvent *) override;

  private:
    QBackingStore m_store{this};
};
