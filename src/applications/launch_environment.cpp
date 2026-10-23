#include "launch_environment.hpp"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QLibraryInfo>
#include <QRegularExpression>
#include <QStandardPaths>
#include <unistd.h>

namespace {
enum class WalletApplication { Other, Code, Chromium, Chrome };
QString withoutJsonComments(QString text)
{
    bool quoted = false, escaped = false, line = false, block = false;
    for (qsizetype i = 0; i < text.size(); ++i) {
        const auto c = text.at(i);
        const auto next = i + 1 < text.size() ? text.at(i + 1) : QChar();
        if (line) {
            if (c == '\n') line = false;
            else text[i] = ' ';
        } else if (block) {
            text[i] = ' ';
            if (c == '*' && next == '/') { text[++i] = ' '; block = false; }
        } else if (quoted) {
            if (escaped) escaped = false;
            else if (c == '\\') escaped = true;
            else if (c == '"') quoted = false;
        } else if (c == '"') {
            quoted = true;
        } else if (c == '/' && (next == '/' || next == '*')) {
            line = next == '/'; block = next == '*'; text[i] = text[i + 1] = ' '; ++i;
        }
    }
    return text;
}
WalletApplication walletApplication(const QString &program)
{
    const auto canonical = QFileInfo(program).canonicalFilePath();
    if (canonical.isEmpty())
        return WalletApplication::Other;
    // Match installed executable paths, never a workspace script merely named "code".
    for (const auto *path : {"/usr/bin/code", "/usr/share/code/bin/code", "/usr/share/code/code"})
        if (canonical == QFileInfo(QString::fromLatin1(path)).canonicalFilePath())
            return WalletApplication::Code;
    for (const auto *path : {"/usr/bin/chromium", "/usr/lib/chromium/chromium"})
        if (canonical == QFileInfo(QString::fromLatin1(path)).canonicalFilePath())
            return WalletApplication::Chromium;
    for (const auto *path : {"/usr/bin/google-chrome", "/usr/bin/google-chrome-stable",
                             "/opt/google/chrome/google-chrome", "/opt/google/chrome/chrome"})
        if (canonical == QFileInfo(QString::fromLatin1(path)).canonicalFilePath())
            return WalletApplication::Chrome;
    return WalletApplication::Other;
}

bool passwordStoreChosen(WalletApplication application, const QStringList &arguments,
                         const QProcessEnvironment &environment)
{
    for (const auto &argument : arguments) {
        if (argument == "--") break;
        if (argument == "--password-store" || argument.startsWith("--password-store=")) return true;
    }
    const auto home = environment.value("HOME", QDir::homePath());
    auto config = environment.value("XDG_CONFIG_HOME");
    if (!QDir::isAbsolutePath(config)) config = QDir(home).filePath(".config");
    const auto flags = application == WalletApplication::Code ? "code-flags.conf" :
                       application == WalletApplication::Chromium ? "chromium-flags.conf" : "chrome-flags.conf";
    QFile source(QDir(config).filePath(flags));
    if (source.open(QIODevice::ReadOnly)) {
        if (source.size() > 65536) return true; // An unreadable preference is not permission to override it.
        static const QRegularExpression option("(^|\\s)--password-store(?:\\s|=|$)");
        for (auto line : QString::fromUtf8(source.readAll()).split('\n'))
            if (option.match(line.section('#', 0, 0)).hasMatch()) return true;
    } else if (source.exists()) {
        return true;
    }
    if (application == WalletApplication::Code) {
        QFile runtime(QDir(home).filePath(".vscode/argv.json"));
        if (runtime.open(QIODevice::ReadOnly)) {
            if (runtime.size() > 131072) return true;
            // argv.json permits comments; a commented example is not an active preference.
            // No global VS Code file is rewritten or migrated here.
            static const QRegularExpression preference("\"password-store\"\\s*:");
            if (preference.match(withoutJsonComments(QString::fromUtf8(runtime.readAll()))).hasMatch()) return true;
        } else if (runtime.exists()) {
            return true;
        }
    }
    return false;
}
}

bool configureKnownApplicationBackend(const QString &program, QStringList &arguments,
                                     const QProcessEnvironment &environment, QString &error,
                                     bool kwalletAvailable, bool requireKWallet)
{
    error.clear();
    const auto application = walletApplication(program);
    if ((kwalletAvailable || requireKWallet) && application != WalletApplication::Other &&
        !passwordStoreChosen(application, arguments, environment)) {
        if (!kwalletAvailable) {
            error = "Saved sign-ins are unavailable. The application was not started with a weaker password store.";
            return false;
        }
        arguments.prepend("--password-store=kwallet6");
    }
    const auto version=QLibraryInfo::version();
    if(version.majorVersion()!=6 || version.minorVersion()!=12 ||
       QFileInfo(program).canonicalFilePath()!=QStringLiteral("/usr/bin/kitty"))
        return true;
    // Later explicit command-line overrides retain the user's chosen backend.
    for(const auto &argument:arguments)
        if(argument.startsWith("linux_display_server=") ||
           argument.startsWith("--override=linux_display_server="))return true;
    if(environment.value("DISPLAY").isEmpty() || environment.value("XAUTHORITY").isEmpty()) {
        error="Kitty needs this session's private X11 service. Konsole remains available.";
        return false;
    }
    arguments=QStringList{"--override","linux_display_server=x11"}+arguments;
    return true;
}

QStringList terminalCommand(const QString &path, const QString &preferred)
{
    const auto directories = path.split(':', Qt::SkipEmptyParts);
    if (!preferred.isEmpty())
        if (const auto program = QStandardPaths::findExecutable(preferred, directories); !program.isEmpty())
            return {program};
    // Each starts in the launch's working folder (the workspace).
    static const QList<QStringList> known{
        {"konsole", "--separate"}, {"xdg-terminal-exec"}, {"gnome-terminal"}, {"kgx"}, {"ptyxis"},
        {"xfce4-terminal"}, {"foot"}, {"alacritty"}, {"kitty"}, {"wezterm", "start"}, {"xterm"}};
    for (auto command : known)
        if (const auto program = QStandardPaths::findExecutable(command.first(), directories); !program.isEmpty())
        {
            command.first() = program;
            return command;
        }
    return {};
}

QProcessEnvironment nativeApplicationEnvironment(QProcessEnvironment environment,
                                                 const QString &socket, const QString &profile,
                                                 const QString &pluginDirectory,
                                                 bool existingApplicationSettings)
{
    const auto absoluteProfile = QDir(profile).absolutePath();
    for (const auto &key :
         {"DISPLAY", "XAUTHORITY", "WAYLAND_SOCKET", "DESKTOP_STARTUP_ID", "XDG_ACTIVATION_TOKEN",
          "QT_STYLE_OVERRIDE", "QT_PLUGIN_PATH", "GTK_THEME"})
        environment.remove(key);
    for (const auto *key : {"PLATINUM_WALLET_LOGIN", "PLATINUM_WALLET_SERVICES",
                           "PLATINUM_WALLET_REQUESTED", "PLATINUM_WALLET_ERROR"})
        environment.remove(QString::fromLatin1(key));
    environment.insert("FRANKENSTEIN_ISOLATED", "1");
    environment.insert("WAYLAND_DISPLAY", socket);
    environment.insert("GDK_BACKEND", "wayland");
    // A private bus has no dconf service. Persist GTK/GIO preferences in this
    // profile's XDG config tree instead of creating shared-memory runtime files.
    environment.insert("GSETTINGS_BACKEND", "keyfile");
    environment.insert("QT_QPA_PLATFORM", "wayland");
    environment.insert("QT_WAYLAND_CLIENT_BUFFER_INTEGRATION", "shm");
    environment.insert("QT_WAYLAND_DISABLE_WINDOWDECORATION", "1");
    environment.insert("QT_QPA_PLATFORMTHEME", "kde");
    environment.insert("XDG_CURRENT_DESKTOP", "Frankenstein");
    environment.insert("XDG_SESSION_DESKTOP", "frankenstein");
    environment.insert("XDG_SESSION_TYPE", "wayland");
    // The desktop's own asset folder (the user's imported Mac OS 9 icons, for the Finder), named
    // before any private data root below replaces XDG_DATA_HOME.
    {
        auto data=environment.value("XDG_DATA_HOME");
        if (!QDir::isAbsolutePath(data)) data=QDir::home().filePath(".local/share");
        environment.insert("PLATINUM_PERSONAL_ASSETS",data+"/platinum-reality/assets");
    }
    // Emulator profiles contain tuned controller mappings and save locations.
    // Keep their existing standard XDG roots while retaining the private display
    // and bus. This is application compatibility, not a security sandbox.
    for (const auto &entry : {qMakePair(QString("XDG_CONFIG_HOME"),QString(".config")),
                              qMakePair(QString("XDG_CACHE_HOME"),QString(".cache")),
                              qMakePair(QString("XDG_DATA_HOME"),QString(".local/share"))}) {
        if (existingApplicationSettings) {
            auto value=environment.value(entry.first);
            if (!QDir::isAbsolutePath(value)) value=QDir::home().filePath(entry.second);
            environment.insert(entry.first,value);
        } else {
            const auto suffix=entry.first=="XDG_CONFIG_HOME" ? "/config" : entry.first=="XDG_CACHE_HOME" ? "/cache" : "/data";
            environment.insert(entry.first,absoluteProfile+suffix);
        }
    }
    const auto audioRuntime=QString("/run/user/%1").arg(getuid());
    const QFileInfo audioDirectory(audioRuntime);
    if (audioDirectory.isDir() && !audioDirectory.isSymLink() && audioDirectory.ownerId()==getuid()) {
        if (QFileInfo::exists(audioRuntime+"/pipewire-0"))
            environment.insert("PIPEWIRE_RUNTIME_DIR",audioRuntime);
        if (QFileInfo::exists(audioRuntime+"/pulse/native"))
            environment.insert("PULSE_SERVER","unix:"+audioRuntime+"/pulse/native");
    }
    if (QFileInfo(pluginDirectory + "/styles/libfrankenstein-style.so").isFile())
    {
        environment.insert("QT_PLUGIN_PATH", QDir(pluginDirectory).absolutePath());
        environment.insert("QT_STYLE_OVERRIDE", "Frankenstein");
    }
    return environment;
}
