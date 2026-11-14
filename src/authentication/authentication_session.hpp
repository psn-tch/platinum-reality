#pragma once
#include <QObject>
#include <QString>

// One PAM conversation for one identity: polkit's PolkitAgentSession in the agent (PolkitSession),
// a scripted one in tests, so the password box's rules are tested without polkitd.
class AuthenticationSession : public QObject
{
    Q_OBJECT
  public:
    using QObject::QObject;
    virtual void initiate() = 0;
    // The typed answer goes straight to PAM; nothing keeps it.
    virtual void respond(const QString &response) = 0;
    virtual void cancel() = 0;

  signals:
    // PAM asks for something ("Password: "); echo is false for secrets.
    void request(const QString &prompt, bool echo);
    // PAM's own words: "Place your finger on the reader", a fingerprint not matched, and so on.
    void message(const QString &text, bool error);
    void completed(bool gainedAuthorization);
};
