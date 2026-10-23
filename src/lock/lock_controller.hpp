#pragma once
#include <QObject>
#include <QString>
#include <QUrl>

class Authenticator;

// What the Log In dialog shows, and its one action. The decision itself belongs to the
// Authenticator (password) or fprintd (finger); this object only reports their results.
class LockController final : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString fullName READ fullName CONSTANT)
    Q_PROPERTY(QUrl picture READ picture CONSTANT)
    Q_PROPERTY(QUrl desktopPicture READ desktopPicture CONSTANT)
    Q_PROPERTY(QString message READ message NOTIFY changed)
    Q_PROPERTY(bool messageIsError READ messageIsError NOTIFY changed)
    Q_PROPERTY(bool ready READ ready NOTIFY changed)
    Q_PROPERTY(bool busy READ busy NOTIFY changed)
    Q_PROPERTY(bool fingerprintAvailable READ fingerprintAvailable NOTIFY changed)
    Q_PROPERTY(int failures READ failures NOTIFY changed)
    // Whether any lock window has the keyboard; the dialog then shows on that screen only.
    Q_PROPERTY(bool keyboardOnLock READ keyboardOnLock NOTIFY changed)
    // Log In at the start of a session (not a lock): the user also chooses the session to start,
    // "platinum" (Platinum Reality) or "kde" (KDE Plasma).
    Q_PROPERTY(bool loginMode READ loginMode CONSTANT)
    Q_PROPERTY(QString session READ session WRITE setSession NOTIFY changed)
  public:
    LockController(Authenticator *authenticator, QString fullName, QUrl picture, QUrl desktopPicture,
                   QObject *parent = nullptr);
    QString fullName() const { return m_fullName; }
    QUrl picture() const { return m_picture; }
    QUrl desktopPicture() const { return m_desktopPicture; }
    QString message() const { return m_message; }
    bool messageIsError() const { return m_messageIsError; }
    bool ready() const { return m_ready; }
    bool busy() const;
    bool fingerprintAvailable() const { return m_fingerprint; }
    int failures() const { return m_failures; }
    bool keyboardOnLock() const { return m_keyboard; }
    bool loginMode() const { return m_loginMode; }
    QString session() const { return m_session; }
    void setSession(const QString &session);
    // Before any window is shown. Unknown sessions start Platinum Reality.
    void setLoginMode(const QString &defaultSession);
    void setKeyboardOnLock(bool keyboard);
    // Ignored until the screen is locked, while checking, or for an empty password.
    Q_INVOKABLE bool submit(const QString &password);
    void setReady(bool ready);
    void setFingerprintAvailable(bool available);
    void showStatus(const QString &text, bool error);

  signals:
    void changed();
    void authenticated();
    // The password field should be cleared and selected for another try.
    void rejected();

  private:
    Authenticator *m_authenticator;
    QString m_fullName, m_message;
    QUrl m_picture, m_desktopPicture;
    bool m_messageIsError = false, m_ready = false, m_fingerprint = false, m_pamMessage = false,
         m_keyboard = false, m_loginMode = false;
    QString m_session = "platinum";
    int m_failures = 0;
};

// The user's picture as the desktop knows it (~/.face.icon, ~/.face, AccountsService), if any.
QUrl userPicture(const QString &user, const QString &home);
// The full name from the password database's GECOS field, or the login name.
QString userFullName(const QString &user, const QString &gecos);
// The desktop picture chosen in the Appearance control panel (the profile's appearance.json),
// if it is a readable file; otherwise empty, and the lock shows a plain Platinum color.
QUrl desktopPictureFrom(const QString &profile, const QString &home);
