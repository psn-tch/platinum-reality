#include "application_appearance.hpp"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>

static void initializeAppearanceResources()
{
    static const bool initialized = []
    {
        Q_INIT_RESOURCE(application_appearance);
        return true;
    }();
    Q_UNUSED(initialized);
}
namespace
{
const QByteArray marker = "/* Platinum Reality application appearance v1.";
// Files written before the project was renamed are ours too, and are replaced like any other.
const QByteArray previousMarker = "/* Platinum Dreams application appearance v1.";
bool regularPath(const QString &root, const QString &relative, QString &error)
{
    QString path = root;
    for (const auto &part : relative.split('/'))
    {
        path += '/' + part;
        const QFileInfo info(path);
        if (info.isSymLink() || (info.exists() && !info.isDir() && path != root + '/' + relative))
        {
            error = "An application appearance path is a symlink or is not a folder: " + path;
            return false;
        }
    }
    return true;
}
bool save(const QString &path, const QByteArray &bytes, QString &error)
{
    QDir parent = QFileInfo(path).absoluteDir();
    if (!parent.mkpath("."))
    {
        error = "The application appearance folder could not be created: " + parent.path();
        return false;
    }
    QSaveFile output(path);
    if (!output.open(QIODevice::WriteOnly) || output.write(bytes) != bytes.size() ||
        !output.commit())
    {
        error = "Application appearance could not be saved: " + output.errorString();
        return false;
    }
    return true;
}
bool managedFile(const QString &root, const QString &relative, const QByteArray &bytes,
                 QString &error)
{
    if (!regularPath(root, relative, error))
        return false;
    const auto path = root + '/' + relative;
    QFile existing(path);
    if (existing.exists())
    {
        if (!existing.open(QIODevice::ReadOnly) || existing.size() > 256 * 1024)
        {
            error = "An application appearance file is unreadable or too large: " + path;
            return false;
        }
        const auto before = existing.readAll();
        if (before == bytes)
            return true;
        if (!before.startsWith(marker) && !before.startsWith(previousMarker))
        {
            error = "An application appearance file has custom contents and was preserved: " + path;
            return false;
        }
    }
    return save(path, bytes, error);
}
QByteArray asset(const char *name)
{
    initializeAppearanceResources();
    QFile file(QString(":/application-appearance/src/appearance/") + name);
    return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray{};
}
bool gtkImport(const QString &root, const QString &version, const QByteArray &css, QString &error)
{
    const auto directory = "config/gtk-" + version;
    if (!managedFile(root, directory + "/platinum-dreams.css", css, error) ||
        !regularPath(root, directory + "/gtk.css", error))
        return false;
    const auto path = root + '/' + directory + "/gtk.css";
    QFile input(path);
    QByteArray before;
    if (input.exists())
    {
        if (!input.open(QIODevice::ReadOnly) || input.size() > 256 * 1024)
        {
            error = "Existing application CSS could not be read: " + path;
            return false;
        }
        before = input.readAll();
    }
    const QByteArray include = "@import url(\"platinum-dreams.css\");";
    if (before.contains(include))
        return true;
    // User rules follow the import and retain their ability to override it.
    return save(path, include + '\n' + before, error);
}
bool firefoxProgram(const QString &program)
{
    const auto executable = QFileInfo(program).canonicalFilePath();
    if (executable.isEmpty())
        return false;
    for (const auto &candidate :
         {"/usr/lib/firefox/firefox", "/usr/lib/firefox/firefox-bin", "/usr/bin/firefox"})
        if (executable == QFileInfo(candidate).canonicalFilePath())
            return true;
    return false;
}
} // namespace

bool prepareApplicationAppearance(const QString &profile, QString &error)
{
    error.clear();
    const auto css = asset("gtk-platinum.css");
    if (css.isEmpty())
    {
        error = "Application appearance resources are unavailable.";
        return false;
    }
    const auto root = QDir(profile).absolutePath();
    return gtkImport(root, "3.0", css, error) && gtkImport(root, "4.0", css, error);
}

QString preparePrivateGtkTheme(const QString &profile, QString &error)
{
    error.clear();
    const auto css = asset("gtk-platinum.css");
    if (css.isEmpty())
    {
        error = "Application appearance resources are unavailable.";
        return {};
    }
    const auto root = QDir(profile).absolutePath();
    // A named theme replaces GTK's default, so each one starts from the built-in base theme.
    for (const auto &[version, base] : {std::pair{"3.0", "resource:///org/gtk/libgtk/theme/Adwaita/gtk-contained.css"},
                                        std::pair{"4.0", "resource:///org/gtk/libgtk/theme/Default/Default-light.css"}})
    {
        const auto directory = QString("share/themes/%1/gtk-%2").arg(privateGtkThemeName, version);
        const QByteArray theme = marker + " Private theme for normal application settings. */\n"
                                          "@import url(\"" + QByteArray(base) + "\");\n"
                                          "@import url(\"platinum-dreams.css\");\n";
        if (!managedFile(root, directory + "/platinum-dreams.css", css, error) ||
            !managedFile(root, directory + "/gtk.css", theme, error))
            return {};
    }
    return root + "/share";
}

bool prepareStyledApplication(frankenstein::applications::LaunchPlan &plan, const QString &profile,
                              QString &error)
{
    error.clear();
    if (!plan || !firefoxProgram(plan.program))
        return true;
    // Respect explicitly configured profiles and alternate startup modes.
    for (const auto &arg : plan.arguments)
    {
        const auto option = arg.toLower().section('=', 0, 0);
        if (option == "-p" || option == "-profile" || option == "--profile" ||
            option == "-profilemanager" || option == "--profilemanager" ||
            option == "-createprofile" || option == "--createprofile" || option == "-headless" ||
            option == "--headless" || option == "-safe-mode" || option == "--safe-mode")
            return true;
    }
    const auto root = QDir(profile).absolutePath();
    if (!regularPath(root, "browsers/firefox", error) || !QDir().mkpath(root + "/browsers/firefox"))
    {
        if (error.isEmpty())
            error = "The Platinum Reality Firefox profile could not be created.";
        return false;
    }
    const auto browser = QFileInfo(root + "/browsers/firefox").canonicalFilePath();
    const auto css = asset("firefox-userChrome.css");
    const QByteArray preferences =
        marker + " Browser UI preferences only. */\n"
                 "user_pref(\"toolkit.legacyUserProfileCustomizations.stylesheets\", true);\n"
                 "user_pref(\"browser.tabs.inTitlebar\", 1);\n";
    if (css.isEmpty() || !managedFile(root, "browsers/firefox/chrome/userChrome.css", css, error) ||
        !managedFile(root, "browsers/firefox/user.js", preferences, error))
        return false;
    // The private session bus and explicit canonical profile scope Firefox's
    // normal same-profile remoting. Do not disable remoting and break repeat opens.
    plan.arguments = QStringList{"--profile", browser} + plan.arguments;
    return true;
}

bool ownedFirefoxProfileArguments(const QStringList &arguments, const QString &profile)
{
    const auto expected =
        QFileInfo(QDir(profile).absoluteFilePath("browsers/firefox")).canonicalFilePath();
    if (expected.isEmpty())
        return false;
    QString selected;
    for (int i = 0; i < arguments.size(); ++i)
    {
        const auto option = arguments[i].toLower().section('=', 0, 0);
        if (option == "-p" || option == "--profilemanager" || option == "-profilemanager")
            return false;
        if (option != "--profile" && option != "-profile")
            continue;
        if (!selected.isEmpty())
            return false;
        const auto path = arguments[i].contains('=') ? arguments[i].section('=', 1)
                          : ++i < arguments.size()   ? arguments[i]
                                                     : QString();
        if (!QDir::isAbsolutePath(path))
            return false;
        selected = QFileInfo(path).canonicalFilePath();
        if (selected.isEmpty())
            return false;
    }
    return selected == expected;
}

bool ownedFirefoxFrame(qint64 peerPid, const QString &profile)
{
    if (peerPid <= 0 || !firefoxProgram(QString("/proc/%1/exe").arg(peerPid)))
        return false;
    QFile commandLine(QString("/proc/%1/cmdline").arg(peerPid));
    if (!commandLine.open(QIODevice::ReadOnly))
        return false;
    const auto bytes = commandLine.read(128 * 1024 + 1);
    if (bytes.isEmpty() || bytes.size() > 128 * 1024 || !bytes.endsWith('\0'))
        return false;
    QStringList arguments;
    const auto tokens = bytes.split('\0');
    for (int i = 1; i + 1 < tokens.size(); ++i)
        arguments.push_back(QFile::decodeName(tokens[i]));
    // Never log argv: it may contain a URL. No browser history/profile contents
    // are inspected. This applies only to the client on our private display.
    return ownedFirefoxProfileArguments(arguments, profile);
}

bool ownedFirefoxLaunch(const QString &program, const QStringList &arguments,
                        const QString &profile)
{
    return firefoxProgram(program) && ownedFirefoxProfileArguments(arguments, profile);
}

bool firefoxClient(qint64 peerPid)
{
    return peerPid > 0 && firefoxProgram(QString("/proc/%1/exe").arg(peerPid));
}

bool firefoxLaunch(const QString &program)
{
    return firefoxProgram(program);
}
