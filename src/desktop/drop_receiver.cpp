#include "drop_receiver_window.hpp"
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QGuiApplication>

int main(int argc, char **argv)
{
    QGuiApplication app(argc, argv);
    if (qEnvironmentVariable("FRANKENSTEIN_ISOLATED") != "1")
        return 64;
    QGuiApplication::setDesktopFileName("org.frankenstein.DropTarget");
    DesktopDropReceiver window;
    QObject::connect(
        &window, &DesktopDropReceiver::filesDropped, &window,
        [&window](const QStringList &paths, const QString &token)
        {
            auto message = QDBusMessage::createMethodCall("org.frankenstein.Desktop", "/Desktop",
                                                          "org.frankenstein.Desktop", "DropFiles");
            message << paths << token;
            auto *watcher = new QDBusPendingCallWatcher(
                QDBusConnection::sessionBus().asyncCall(message, 5000), &window);
            QObject::connect(watcher, &QDBusPendingCallWatcher::finished, &window,
                             [watcher]
                             {
                                 const QDBusPendingReply<bool> reply = *watcher;
                                 if (reply.isError() || !reply.value())
                                     qWarning("The desktop file drop was not accepted.");
                                 if (qEnvironmentVariable("FRANKENSTEIN_NATIVE_TEST") == "1")
                                     qInfo("Drop bridge reply: error=%d accepted=%d",
                                           reply.isError(), !reply.isError() && reply.value());
                                 watcher->deleteLater();
                             });
        });
    window.show();
    return app.exec();
}
