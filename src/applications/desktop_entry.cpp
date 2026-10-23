#include "desktop_entry.hpp"
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QStandardPaths>
#include <QUrl>
#include <glib.h>
#include <memory>

namespace frankenstein::applications
{
namespace
{
QString value(GKeyFile *file, const char *key, bool localized = false)
{
    gchar *text = localized ? g_key_file_get_locale_string(file, "Desktop Entry", key, nullptr, nullptr)
                            : g_key_file_get_string(file, "Desktop Entry", key, nullptr);
    const auto result = QString::fromUtf8(text ? text : "");
    g_free(text);
    return result;
}
QStringList list(GKeyFile *file, const char *key)
{
    gsize count = 0;
    auto **strings = g_key_file_get_string_list(file, "Desktop Entry", key, &count, nullptr);
    QStringList result;
    for (gsize i = 0; strings && i < count && i < 1024; ++i)
        result.push_back(QString::fromUtf8(strings[i]));
    g_strfreev(strings);
    return result;
}
bool flag(GKeyFile *file, const char *key)
{
    return g_key_file_get_boolean(file, "Desktop Entry", key, nullptr);
}
QString executable(const QString &program, const QProcessEnvironment &environment)
{
    if (program.isEmpty() || program.contains('='))
        return {};
    if (QDir::isAbsolutePath(program))
        return QFileInfo(program).isFile() && QFileInfo(program).isExecutable() ? program : QString();
    if (program.contains('/'))
        return {};
    return QStandardPaths::findExecutable(program, environment.value("PATH").split(':', Qt::SkipEmptyParts));
}
QStringList tokenize(const QString &command, QString &error)
{
    QStringList result;
    QString token;
    bool quoted = false, escaped = false, started = false;
    for (qsizetype i = 0; i < command.size(); ++i)
    {
        const auto c = command[i];
        if (c.unicode() < 32 && c != '\t')
        {
            error = "The application command contains a control character.";
            return {};
        }
        if (escaped)
        {
            if (quoted && c != '"' && c != '`' && c != '$' && c != '\\')
                token += '\\';
            token += c;
            escaped = false;
        }
        else if (c == '\\')
        {
            escaped = true;
            started = true;
        }
        else if (c == '"')
        {
            quoted = !quoted;
            started = true;
        }
        else if (!quoted && c.isSpace())
        {
            if (started)
                result.push_back(token);
            token.clear();
            started = false;
        }
        else
        {
            if (!quoted && QString("'<>~|&;$*?#()`").contains(c))
            {
                error = "Special characters in the application command must be quoted.";
                return {};
            }
            if (quoted && c == '%' && i + 1 < command.size() && command[i + 1] != '%')
            {
                error = "Field codes inside quoted arguments are not supported.";
                return {};
            }
            if (quoted && c == '%' && i + 1 < command.size() && command[i + 1] == '%')
            {
                token += "%%";
                ++i;
            }
            else
                token += c;
            started = true;
        }
    }
    if (quoted || escaped)
        error = "The application command has an unfinished quote or escape.";
    else if (started)
        result.push_back(token);
    if (result.isEmpty() && error.isEmpty())
        error = "This application has no executable command.";
    return result;
}
bool validateFields(DesktopEntry &entry)
{
    int documentFields = 0;
    for (qsizetype t = 0; t < entry.tokens.size(); ++t)
    {
        const auto &token = entry.tokens[t];
        for (qsizetype i = 0; i < token.size(); ++i)
        {
            if (token[i] != '%')
                continue;
            if (++i == token.size())
                return false;
            const auto c = token[i].toLatin1();
            if (c == '%')
                continue;
            if (t == 0 || !QByteArray("fFuUickdDnNvm").contains(c))
                return false;
            if (c == 'F' || c == 'U' || c == 'i')
                if (token != "%" + QString(QChar::fromLatin1(c)))
                    return false;
            if (QByteArray("fFuU").contains(c))
            {
                entry.documentCode = c;
                ++documentFields;
            }
        }
    }
    return documentFields <= 1;
}
} // namespace

DesktopEntry readDesktopEntry(const QString &filename, const QString &id,
                               const QProcessEnvironment &environment)
{
    DesktopEntry entry;
    entry.id = id;
    entry.filename = QFileInfo(filename).absoluteFilePath();
    QFile input(filename);
    if (!input.open(QIODevice::ReadOnly) || input.size() > 512 * 1024)
    {
        entry.unavailable = "This application entry cannot be read.";
        return entry;
    }
    const auto data = input.read(512 * 1024 + 1);
    entry.sourceBytes = data.size();
    if (data.size() > 512 * 1024 || input.error() != QFileDevice::NoError)
    {
        entry.unavailable = "This application entry is too large or cannot be read.";
        return entry;
    }
    entry.fingerprint = QCryptographicHash::hash(data, QCryptographicHash::Sha256);
    std::unique_ptr<GKeyFile, decltype(&g_key_file_unref)> file(g_key_file_new(), g_key_file_unref);
    if (!g_key_file_load_from_data(file.get(), data.constData(), data.size(), G_KEY_FILE_NONE, nullptr) ||
        value(file.get(), "Type") != "Application")
    {
        entry.unavailable = "This is not a valid application entry.";
        return entry;
    }
    entry.hidden = flag(file.get(), "Hidden");
    entry.noDisplay = flag(file.get(), "NoDisplay");
    const auto only = list(file.get(), "OnlyShowIn"), except = list(file.get(), "NotShowIn");
    entry.showHere = (only.isEmpty() || only.contains("Frankenstein")) && !except.contains("Frankenstein");
    entry.name = value(file.get(), "Name", true).left(512);
    entry.comment = value(file.get(), "Comment", true).left(4096);
    entry.genericName = value(file.get(), "GenericName", true).left(512);
    entry.keywords = list(file.get(), "Keywords");
    entry.mimeTypes = list(file.get(), "MimeType");
    entry.icon = value(file.get(), "Icon").left(4096);
    entry.workingDirectory = value(file.get(), "Path");
    entry.terminal = flag(file.get(), "Terminal");
    entry.emulator = list(file.get(), "Categories").contains("Emulator");
    const auto command = value(file.get(), "Exec");
    if (entry.name.isEmpty() || command.size() > 16384)
        entry.unavailable = "The application entry has no name or its command is too large.";
    else if (command.isEmpty() && flag(file.get(), "DBusActivatable"))
        entry.unavailable = "This application requires D-Bus activation without an executable fallback.";
    else
        entry.tokens = tokenize(command, entry.unavailable);
    if (entry.unavailable.isEmpty() && !validateFields(entry))
        entry.unavailable = "The application command has an invalid field code.";
    const auto tryExec = value(file.get(), "TryExec");
    if (!tryExec.isEmpty() && executable(tryExec, environment).isEmpty())
        entry.unavailable = "This application's required executable is not installed.";
    return entry;
}

LaunchPlan planLaunch(const DesktopEntry &entry, const QStringList &documents,
                        const QString &workspace, const QProcessEnvironment &environment,
                        const QString &terminal)
{
    LaunchPlan plan;
    plan.existingApplicationSettings = entry.emulator;
    plan.error = entry.unavailable;
    if (!plan.error.isEmpty())
        return plan;
    auto checked = entry;
    checked.documentCode = 0;
    if (entry.tokens.isEmpty() || entry.tokens.first().isEmpty() || !validateFields(checked))
    {
        plan.error = "This application has no executable command.";
        return plan;
    }
    if (!documents.isEmpty() && checked.documentCode == 0)
        plan.error = "This application does not accept documents.";
    else if (documents.size() > 1 && (checked.documentCode == 'f' || checked.documentCode == 'u'))
        plan.error = "This application accepts one document at a time.";
    if (!plan.error.isEmpty())
        return plan;
    QStringList expanded;
    for (const auto &token : entry.tokens)
    {
        if (token == "%F" || token == "%U")
        {
            for (const auto &path : documents)
                expanded.push_back(token == "%F" ? path : QUrl::fromLocalFile(path).toString(QUrl::FullyEncoded));
            continue;
        }
        if (token == "%i")
        {
            if (!entry.icon.isEmpty())
                expanded << "--icon" << entry.icon;
            continue;
        }
        if ((token == "%f" || token == "%u") && documents.isEmpty())
            continue;
        QString arg;
        for (qsizetype i = 0; i < token.size(); ++i)
        {
            if (token[i] != '%')
            {
                arg += token[i];
                continue;
            }
            const auto c = token[++i].toLatin1(); // Validated before publication.
            if (c == '%') arg += '%';
            else if (c == 'c') arg += entry.name;
            else if (c == 'k') arg += entry.filename;
            else if (c == 'f' && !documents.isEmpty()) arg += documents.first();
            else if (c == 'u' && !documents.isEmpty()) arg += QUrl::fromLocalFile(documents.first()).toString(QUrl::FullyEncoded);
        }
        if (!arg.isEmpty() || token.isEmpty())
            expanded.push_back(arg);
    }
    plan.program = expanded.isEmpty() ? QString() : executable(expanded.takeFirst(), environment);
    if (plan.program.isEmpty())
    {
        plan.error = "This application's executable is not installed or cannot be run.";
        return plan;
    }
    plan.arguments = expanded;
    plan.workingDirectory = entry.workingDirectory.isEmpty() ? workspace : entry.workingDirectory;
    if (!QDir::isAbsolutePath(plan.workingDirectory) || !QFileInfo(plan.workingDirectory).isDir())
    {
        plan.error = "This application's working folder is unavailable.";
        return plan;
    }
    if (entry.terminal)
    {
        const auto wrapper = executable(terminal, environment);
        if (wrapper.isEmpty())
            plan.error = "A supported terminal is required to open this application.";
        else
        {
            plan.arguments = QStringList{"--separate", "--workdir", plan.workingDirectory, "-e", plan.program} + plan.arguments;
            plan.program = wrapper;
        }
    }
    return plan;
}
QStringList applicationDirectories()
{
    auto directories = QStandardPaths::standardLocations(QStandardPaths::ApplicationsLocation);
    directories.removeDuplicates();
    return directories;
}
} // namespace frankenstein::applications
