#pragma once
#include <QDialog>
#include <QList>
#include <QPair>

class QComboBox;
class QLabel;
class QPushButton;
class PasswordField;

// The password box (Lily Hewitt, 2026-11-14: "make the polkit more 9.2.2 style"). The controller
// uses only this interface; the look follows Mac OS 9.2.2's password dialogs (docs/password-box.md).
// Object names are stable for tests: authenticationMessage, authenticationName, authenticationPassword,
// authenticationStatus, authenticationOK, authenticationCancel, authenticationFailed (the stop alert).
class AuthenticationDialog final : public QDialog
{
    Q_OBJECT
  public:
    // identities: (polkit identity, name shown), in polkit's order.
    AuthenticationDialog(const QString &message, const QString &iconName,
                         const QList<QPair<QString, QString>> &identities, const QString &selected,
                         QWidget *parent = nullptr);
    // PAM asks: the field takes the answer and OK sends it.
    void ask(const QString &prompt, bool echo);
    // PAM's own information or error text, shown under the fields.
    void showMessage(const QString &text, bool error);
    // The answer was wrong: a stop alert says so over the box, and the field asks again.
    void rejectAnswer(const QString &why);
    // While PAM decides, nothing can be typed or sent twice.
    void setWaiting(bool waiting);
    QString selectedIdentity() const;

  signals:
    void answered(const QString &response);
    void identityChosen(const QString &identity);
    void canceled();

  public slots:
    void reject() override;

  private:
    void send();
    QString shownName() const;
    QList<QPair<QString, QString>> m_identities;
    QComboBox *m_names = nullptr;
    QLabel *m_name = nullptr;
    QLabel *m_prompt;
    PasswordField *m_answer;
    QLabel *m_status;
    QPushButton *m_ok;
    QString m_identity;
    bool m_asking = false;
    // How many characters the last answer had, for the selected bullets after a wrong one. Never the answer.
    int m_sentLength = 0;
};
