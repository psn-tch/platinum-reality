#include "polkit_agent.hpp"
#include "authentication_controller.hpp"
#include <PolkitQt1/Agent/Session>
#include <PolkitQt1/Details>
#include <PolkitQt1/Identity>

PolkitSession::PolkitSession(const QString &identity, const QString &cookie, QObject *parent)
    : AuthenticationSession(parent),
      m_session(new PolkitQt1::Agent::Session(PolkitQt1::Identity::fromString(identity), cookie, nullptr, this))
{
    connect(m_session, &PolkitQt1::Agent::Session::request, this, [this](const QString &prompt, bool echo) {
        qInfo("PAM asks: %s", qUtf8Printable(prompt.trimmed())); // The question only, never the answer.
        emit request(prompt, echo);
    });
    connect(m_session, &PolkitQt1::Agent::Session::completed, this, &AuthenticationSession::completed);
    connect(m_session, &PolkitQt1::Agent::Session::showInfo, this,
            [this](const QString &text) { emit message(text, false); });
    connect(m_session, &PolkitQt1::Agent::Session::showError, this,
            [this](const QString &text) { emit message(text, true); });
}

void PolkitSession::initiate()
{
    m_session->initiate();
}

void PolkitSession::respond(const QString &response)
{
    m_session->setResponse(response);
}

void PolkitSession::cancel()
{
    m_session->cancel();
}

PolkitAgent::PolkitAgent(AuthenticationController &controller, QObject *parent)
    : Listener(parent), m_controller(controller)
{
}

void PolkitAgent::initiateAuthentication(const QString &actionId, const QString &message, const QString &iconName,
                                         const PolkitQt1::Details &, const QString &cookie,
                                         const PolkitQt1::Identity::List &identities,
                                         PolkitQt1::Agent::AsyncResult *result)
{
    AuthenticationRequest request{actionId, message, iconName, cookie, {}};
    for (const auto &identity : identities)
        request.identities.append(identity.toString());
    qInfo("Password box for %s", qUtf8Printable(actionId));
    if (!m_controller.begin(request, [result] { result->setCompleted(); }))
    {
        // One box at a time, as KDE's agent does; polkit answers that client "not authorized".
        result->setError("Another password box is already open.");
        result->setCompleted();
    }
}

bool PolkitAgent::initiateAuthenticationFinish()
{
    return true;
}

void PolkitAgent::cancelAuthentication()
{
    qInfo("polkit withdrew the request");
    m_controller.cancel();
}
