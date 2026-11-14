#pragma once
#include "authentication_dialog.hpp"
#include <QObject>
#include <QStringList>
#include <functional>

class AuthenticationSession;

struct AuthenticationRequest
{
    QString actionId, message, iconName, cookie;
    // polkit identities as strings ("unix-user:lily", "unix-group:wheel"), in polkit's order.
    QStringList identities;
};

// What one password box does between polkit's request and its answer: who is asked (the person
// logged in when polkit allows them, as Mac OS 9 fills in the owner's name), wrong answers asked
// again up to three times (pam_faillock's default locks the account after three), and Cancel.
// PAM's conversation starts only when OK is pressed (answerFirst): a conversation ended unanswered,
// by Cancel or by polkit withdrawing the request, counts as a failed password in pam_faillock
// (two such records on 2026-11-14 while testing). With fingerprints in polkit's PAM stack the
// conversation starts at once instead, so the reader can be used without typing anything.
class AuthenticationController final : public QObject
{
    Q_OBJECT
  public:
    using SessionFactory =
        std::function<AuthenticationSession *(const QString &identity, const QString &cookie, QObject *parent)>;
    static constexpr int attempts = 3;
    explicit AuthenticationController(SessionFactory factory, bool answerFirst = true, QString user = {},
                                      QObject *parent = nullptr);
    ~AuthenticationController() override;
    // false while a box is already up: polkit then tells that client no.
    // finished runs once, when the box is answered, canceled or withdrawn.
    bool begin(const AuthenticationRequest &request, std::function<void()> finished);
    // polkit withdrew the request (the asking program quit or timed out).
    void cancel();
    AuthenticationDialog *dialog() const;
    static QString displayName(const QString &identity);

  signals:
    void dialogShown(AuthenticationDialog *dialog);

  private:
    void startSession();
    void dropSession();
    void askAgain();
    void sessionCompleted(bool gained);
    void finish();
    SessionFactory m_factory;
    bool m_answerFirst;
    QString m_user;
    // An answer typed before PAM asked, held only until PAM's first question (answerFirst).
    QString m_answer;
    AuthenticationRequest m_request;
    QString m_identity;
    AuthenticationDialog *m_dialog = nullptr; // Only finish() and the destructor delete it.
    AuthenticationSession *m_session = nullptr;
    std::function<void()> m_finished;
    int m_failures = 0;
};
