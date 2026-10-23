#pragma once
#include <QProcessEnvironment>
#include <QString>
#include <QStringList>

// Scoped workaround for kitty's unconditional v3 data-device binding against
// Qt 6.12's v1 server. Uses only the session's private, authenticated X11 service.
bool configureKnownApplicationBackend(const QString &program, QStringList &arguments,
                                     const QProcessEnvironment &environment, QString &error,
                                     bool kwalletAvailable = false, bool requireKWallet = false);

// The Apple menu's Terminal: $TERMINAL when it names an installed program, else the first
// installed of Konsole, xdg-terminal-exec and common terminals, searched in `path` (PATH's
// format). The program comes first; empty when none is installed.
QStringList terminalCommand(const QString &path, const QString &preferred = {});

QProcessEnvironment nativeApplicationEnvironment(QProcessEnvironment environment,
                                                   const QString &socket,
                                                   const QString &profile,
                                                   const QString &pluginDirectory,
                                                   bool existingApplicationSettings = false);
