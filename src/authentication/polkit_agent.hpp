#pragma once
#include "authentication_session.hpp"
#include <PolkitQt1/Agent/Listener>

class AuthenticationController;
namespace PolkitQt1::Agent
{
class Session;
}

// polkit's own conversation (PolkitAgentSession runs polkit-agent-helper-1, which talks to PAM).
class PolkitSession final : public AuthenticationSession
{
    Q_OBJECT
  public:
    PolkitSession(const QString &identity, const QString &cookie, QObject *parent = nullptr);
    void initiate() override;
    void respond(const QString &response) override;
    void cancel() override;

  private:
    PolkitQt1::Agent::Session *m_session;
};

// The session's polkit authentication agent: each request becomes one password box.
class PolkitAgent final : public PolkitQt1::Agent::Listener
{
    Q_OBJECT
  public:
    explicit PolkitAgent(AuthenticationController &controller, QObject *parent = nullptr);

  public slots:
    void initiateAuthentication(const QString &actionId, const QString &message, const QString &iconName,
                                const PolkitQt1::Details &details, const QString &cookie,
                                const PolkitQt1::Identity::List &identities,
                                PolkitQt1::Agent::AsyncResult *result) override;
    bool initiateAuthenticationFinish() override;
    void cancelAuthentication() override;

  private:
    AuthenticationController &m_controller;
};
