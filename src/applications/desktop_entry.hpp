#pragma once
#include <QProcessEnvironment>
#include <QStringList>

namespace frankenstein::applications
{
struct DesktopEntry
{
    QString id, filename, name, comment, genericName, icon, workingDirectory;
    QStringList keywords, mimeTypes, tokens;
    QByteArray fingerprint;
    qint64 sourceBytes = 0;
    QString unavailable;
    bool hidden = false, noDisplay = false, showHere = true, terminal = false;
    bool emulator = false;
    char documentCode = 0;
};
struct LaunchPlan
{
    QString program;
    QStringList arguments;
    QString workingDirectory, error;
    bool existingApplicationSettings = false;
    explicit operator bool() const { return error.isEmpty() && !program.isEmpty(); }
};
DesktopEntry readDesktopEntry(const QString &filename, const QString &id,
                               const QProcessEnvironment &environment);
LaunchPlan planLaunch(const DesktopEntry &, const QStringList &documents,
                        const QString &workspace, const QProcessEnvironment &environment,
                        const QString &terminal = "/usr/bin/konsole");
QStringList applicationDirectories();
} // namespace frankenstein::applications
