#pragma once
#include <QObject>
#include <QProcess>
#include <QTimer>
#include <QVariantMap>

class SystemControls final : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QVariantMap state READ state NOTIFY stateChanged)
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)
    Q_PROPERTY(QString error READ error NOTIFY errorChanged)
  public:
    explicit SystemControls(QString helper, QObject *parent = nullptr);
    ~SystemControls() override;
    QVariantMap state() const;
    bool busy() const { return m_busy; }
    QString error() const { return m_error; }
    Q_INVOKABLE void refresh();
    Q_INVOKABLE void set(const QString &control, int value);
  signals:
    void stateChanged();
    void busyChanged();
    void errorChanged();
  private:
    void start(const QStringList &arguments);
    void startPending();
    QString m_helper, m_error;
    // Requests made while the helper runs: the latest value per control, in
    // first-request order, then an optional status refresh.
    QList<QPair<QString, int>> m_pending;
    bool m_refreshPending = false;
    QVariantMap m_state;
    // Display the newest user request until it is acknowledged. Older helper
    // replies must not undo newer slider moves; failures restore reported state.
    QMap<QString, int> m_requested;
    QString m_activeControl;
    int m_activeValue = 0;
    QProcess m_process;
    QTimer m_timeout;
    bool m_busy = false;
    bool m_timedOut = false;
};
