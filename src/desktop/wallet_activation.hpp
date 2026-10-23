#pragma once
#include <QDBusConnection>
#include <QMap>
#include <QObject>
#include <QProcessEnvironment>
#include <QTimer>

// Configure only the private session bus. Readiness does not start/open a wallet:
// an application's first KWallet call activates the installed service on demand.
class WalletActivation final : public QObject
{
    Q_OBJECT
  public:
    explicit WalletActivation(QDBusConnection bus, QObject *parent = nullptr);
    void configure(const QProcessEnvironment &environment);
    bool pending() const { return m_pending; }
    bool ready() const { return m_ready; }
    QString error() const { return m_error; }
    static QMap<QString, QString> activationEnvironment(const QProcessEnvironment &environment);
  signals:
    void changed();
  private:
    QDBusConnection m_bus;
    QTimer m_watch;
    quint64 m_generation = 0;
    bool m_pending = false, m_ready = false;
    QString m_error;
};
