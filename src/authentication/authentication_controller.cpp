#include "authentication_controller.hpp"
#include "authentication_dialog.hpp"
#include "authentication_session.hpp"
#include <pwd.h>
#include <unistd.h>
#include <utility>

namespace
{
QString loggedInUser()
{
    const auto *entry = getpwuid(getuid());
    return entry ? QString::fromLocal8Bit(entry->pw_name) : QString();
}
} // namespace

AuthenticationController::AuthenticationController(SessionFactory factory, bool answerFirst, QString user,
                                                   QObject *parent)
    : QObject(parent), m_factory(std::move(factory)), m_answerFirst(answerFirst),
      m_user(user.isEmpty() ? loggedInUser() : std::move(user))
{
}

AuthenticationController::~AuthenticationController()
{
    dropSession();
    delete m_dialog;
}

AuthenticationDialog *AuthenticationController::dialog() const
{
    return m_dialog;
}

QString AuthenticationController::displayName(const QString &identity)
{
    // The full name from the account when it has one, like the Name field of 9.2.2's boxes.
    if (identity.startsWith("unix-user:"))
    {
        const auto name = identity.mid(10);
        const auto *entry = getpwnam(name.toLocal8Bit().constData());
        const auto full = entry && entry->pw_gecos ? QString::fromLocal8Bit(entry->pw_gecos).section(',', 0, 0).trimmed()
                                                   : QString();
        return full.isEmpty() ? name : full;
    }
    if (identity.startsWith("unix-group:"))
        return "Group " + identity.mid(11);
    return identity;
}

bool AuthenticationController::begin(const AuthenticationRequest &request, std::function<void()> finished)
{
    if (m_finished || m_dialog)
        return false;
    m_request = request;
    m_finished = std::move(finished);
    m_failures = 0;
    const auto own = "unix-user:" + m_user;
    m_identity = request.identities.contains(own) ? own : request.identities.value(0);
    QList<QPair<QString, QString>> names;
    for (const auto &identity : request.identities)
        names.append({identity, displayName(identity)});
    m_dialog = new AuthenticationDialog(request.message, request.iconName, names, m_identity);
    connect(m_dialog, &AuthenticationDialog::answered, this, [this](const QString &response) {
        m_dialog->setWaiting(true);
        if (m_session)
            m_session->respond(response);
        else
        {
            m_answer = response;
            startSession();
        }
    });
    connect(m_dialog, &AuthenticationDialog::identityChosen, this, [this](const QString &identity) {
        if (identity == m_identity)
            return;
        m_identity = identity;
        m_failures = 0;
        dropSession();
        askAgain();
    });
    connect(m_dialog, &AuthenticationDialog::canceled, this, &AuthenticationController::finish);
    if (m_identity.isEmpty())
        m_dialog->showMessage("No one may authorize this.", true);
    else
        askAgain();
    m_dialog->show();
    m_dialog->raise();
    m_dialog->activateWindow();
    emit dialogShown(m_dialog);
    return true;
}

void AuthenticationController::cancel()
{
    finish();
}

void AuthenticationController::startSession()
{
    m_session = m_factory(m_identity, m_request.cookie, this);
    if (!m_session)
    {
        m_dialog->showMessage("The password could not be checked.", true);
        return;
    }
    m_dialog->setWaiting(true);
    connect(m_session, &AuthenticationSession::request, this, [this](const QString &prompt, bool echo) {
        // PAM's first secret question takes the answer already typed; anything else is asked.
        if (!echo && !m_answer.isEmpty())
            m_session->respond(std::exchange(m_answer, {}));
        else if (m_dialog)
            m_dialog->ask(prompt, echo);
    });
    connect(m_session, &AuthenticationSession::message, this, [this](const QString &text, bool error) {
        if (m_dialog)
            m_dialog->showMessage(text, error);
    });
    connect(m_session, &AuthenticationSession::completed, this, &AuthenticationController::sessionCompleted);
    m_session->initiate();
}

void AuthenticationController::askAgain()
{
    if (m_answerFirst)
        m_dialog->ask("Password:", false);
    else
        startSession();
}

void AuthenticationController::dropSession()
{
    m_answer.clear();
    if (!m_session)
        return;
    auto *session = std::exchange(m_session, nullptr);
    disconnect(session, nullptr, this, nullptr);
    session->cancel();
    session->deleteLater();
}

void AuthenticationController::sessionCompleted(bool gained)
{
    // A finished session is not canceled again; the next attempt gets a new one, as polkit requires.
    m_answer.clear();
    if (auto *session = std::exchange(m_session, nullptr))
    {
        disconnect(session, nullptr, this, nullptr);
        session->deleteLater();
    }
    if (gained || !m_dialog || ++m_failures >= attempts)
    {
        finish();
        return;
    }
    m_dialog->rejectAnswer("Sorry, that password is not correct. Please try again.");
    askAgain();
}

void AuthenticationController::finish()
{
    dropSession();
    if (m_dialog)
    {
        m_dialog->hide();
        m_dialog->deleteLater();
        m_dialog = nullptr;
    }
    if (auto finished = std::exchange(m_finished, nullptr))
        finished();
}
