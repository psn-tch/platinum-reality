#pragma once
#include <QDBusConnection>
#include <QObject>
#include <QString>
#include <functional>

// Fingerprint unlock through fprintd's D-Bus API: claim the default reader for the user and
// keep verifying until a match, so a finger works without first pressing Return. While the
// reader is claimed here, pam_fprintd in the password stack cannot claim it and steps aside.
// The bus is injectable so qualification can use a fake fprintd on a private bus.
class Fingerprint final : public QObject
{
    Q_OBJECT
  public:
    Fingerprint(QString user, QDBusConnection bus, QObject *parent = nullptr);
    ~Fingerprint() override;
    void start();
    // Stops verifying and releases the reader; synchronous, bounded by a short timeout.
    void stop();
    bool available() const { return m_verifying; }

  signals:
    void availableChanged(bool available);
    void status(const QString &text);
    void matched();

  private slots:
    void verifyStatus(const QString &result, bool done);

  private:
    void call(const QString &method, const QVariantList &arguments,
              std::function<void(const QDBusMessage &)> done, const QString &path = {});
    void verify();
    void unavailable(const QString &reason);
    void release();
    QString m_user, m_device;
    QDBusConnection m_bus;
    bool m_claimed = false, m_verifying = false, m_stopped = false;
    int m_errors = 0;
};
