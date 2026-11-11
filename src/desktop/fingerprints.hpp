#pragma once
#include <QDBusConnection>
#include <QDBusMessage>
#include <QObject>
#include <QProcess>
#include <QStringList>
#include <functional>

// The Fingerprints control panel's service (docs/fingerprints.md; Lily Hewitt, 2026-11-10: "i wanna
// use my finger print to log in"). Fingers are enrolled and deleted with fprintd over the system bus,
// as the user (fprintd asks for the user's own password through the polkit agent). Whether Log In,
// the lock and password boxes accept a finger is a PAM change that only the root helper
// platinum-fingerprint-login makes, through pkexec. The bus, helper and pkexec are injectable for tests.
class FingerprintService final : public QObject
{
    Q_OBJECT
    // fprintd is installed (its D-Bus service exists).
    Q_PROPERTY(bool installed READ installed NOTIFY changed)
    // The default reader's name; empty when there is none.
    Q_PROPERTY(QString reader READ reader NOTIFY changed)
    // "press" or "swipe"
    Q_PROPERTY(QString scanType READ scanType NOTIFY changed)
    // fprintd's finger names ("right-index-finger"), in fprintd's order.
    Q_PROPERTY(QStringList fingers READ fingers NOTIFY changed)
    Q_PROPERTY(bool busy READ busy NOTIFY changed)
    // Adding a finger: which one, and how many of the reader's scans are done.
    Q_PROPERTY(QString enrolling READ enrolling NOTIFY changed)
    Q_PROPERTY(int stage READ stage NOTIFY changed)
    Q_PROPERTY(int stages READ stages NOTIFY changed)
    // What to do now ("Lift your finger and touch the reader again."), or what went wrong.
    Q_PROPERTY(QString message READ message NOTIFY changed)
    Q_PROPERTY(QString error READ error NOTIFY changed)
    // The PAM side, from the helper: fprintd's PAM module exists; Log In/lock/password boxes; sudo.
    Q_PROPERTY(bool loginAvailable READ loginAvailable NOTIFY changed)
    Q_PROPERTY(bool login READ login NOTIFY changed)
    Q_PROPERTY(bool sudo READ sudo NOTIFY changed)
    // The helper is running (an administrator's password may be on screen).
    Q_PROPERTY(bool changing READ changing NOTIFY changed)
  public:
    // `elevate` runs the helper as root ({"pkexec"}); empty runs it directly (tests).
    FingerprintService(QDBusConnection bus, QString helper, QStringList elevate = {"pkexec"},
                       QObject *parent = nullptr);
    ~FingerprintService() override;
    bool installed() const { return m_installed; }
    QString reader() const { return m_reader; }
    QString scanType() const { return m_scanType; }
    QStringList fingers() const { return m_fingers; }
    bool busy() const { return m_pending > 0 || !m_enrolling.isEmpty(); }
    QString enrolling() const { return m_enrolling; }
    int stage() const { return m_stage; }
    int stages() const { return m_stages; }
    QString message() const { return m_message; }
    QString error() const { return m_error; }
    bool loginAvailable() const { return m_loginAvailable; }
    bool login() const { return m_login; }
    bool sudo() const { return m_sudo; }
    bool changing() const { return m_helper.state() != QProcess::NotRunning; }
    Q_INVOKABLE void refresh();
    Q_INVOKABLE void enroll(const QString &finger);
    Q_INVOKABLE void cancel();
    Q_INVOKABLE void remove(const QString &finger);
    Q_INVOKABLE void setLogin(bool login, bool sudo);
    Q_INVOKABLE void installSupport();
    // "right-index-finger" -> "Right Index Finger"
    Q_INVOKABLE static QString fingerTitle(const QString &finger);
    static QStringList allFingers();

  signals:
    void changed();
    // A finger was added (the panel selects it).
    void enrolled(const QString &finger);

  private slots:
    void enrollStatus(const QString &result, bool done);

  private:
    void call(const QString &method, const QVariantList &arguments, std::function<void(const QDBusMessage &)> done,
              bool interactive = false, const QString &path = {}, const QString &interface = {});
    void claimed(std::function<void()> then);
    void release();
    void finishEnroll(const QString &error);
    void failed(const QDBusMessage &reply, const QString &doing);
    void runHelper(const QStringList &arguments, bool elevated);
    void helperFinished(QProcess *process, int code, QProcess::ExitStatus status);
    QDBusConnection m_bus;
    QString m_helperPath;
    QStringList m_elevate;
    QProcess m_status, m_helper;
    QString m_device, m_reader, m_scanType, m_enrolling, m_message, m_error;
    QStringList m_fingers;
    int m_stage = 0, m_stages = 0, m_pending = 0, m_generation = 0;
    bool m_installed = false, m_claimed = false, m_loginAvailable = false, m_login = false, m_sudo = false;
};
