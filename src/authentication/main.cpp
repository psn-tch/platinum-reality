#include "appearance/interface_font.hpp"
#include "authentication_controller.hpp"
#include "finder/platinum_style.hpp"
#include "lock/authenticator.hpp"
#include "polkit_agent.hpp"
#include <PolkitQt1/Subject>
#include <QApplication>
#include <cstdio>

// Platinum Dreams's polkit authentication agent: the password box for Software Update, the App Store,
// Fingerprints and anything else that uses pkexec in this session (docs/password-box.md). The desktop
// starts it once per session; KDE's agent is the fallback when it is not installed.
int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    app.setApplicationName("Password");
    app.setDesktopFileName("org.platinumreality.Authentication");
    app.setStyle(new PlatinumStyle);
    app.setFont(classicInterfaceFont());
    app.setQuitOnLastWindowClosed(false);
    AuthenticationController controller(
        [](const QString &identity, const QString &cookie, QObject *parent) -> AuthenticationSession *
        { return new PolkitSession(identity, cookie, parent); },
        !pamServiceUsesFingerprint("polkit-1"));
    PolkitAgent agent(controller);
    // Only this login session's requests: the subject is the session this process belongs to.
    const PolkitQt1::UnixSessionSubject session(QCoreApplication::applicationPid());
    if (!agent.registerListener(session, "/org/platinumreality/AuthenticationAgent"))
    {
        std::fprintf(stderr, "platinum-authentication-agent: polkit did not accept this agent for session %s "
                             "(another agent may already answer for it).\n",
                     qUtf8Printable(session.toString()));
        return 1;
    }
    return app.exec();
}
