#include "locale_settings.hpp"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QLocale>
#include <QProcess>
#include <QRegularExpression>
#include <QSaveFile>

#include <algorithm>

namespace
{
// en_US.utf8 and en_US.UTF-8 are the same locale; locale.conf spells it the second way.
QString normalized(const QString &name)
{
    static const QRegularExpression pattern("^([^.@]+)(?:\\.([^@]*))?(@.*)?$");
    const auto match = pattern.match(name.trimmed());
    if (!match.hasMatch())
        return name.trimmed();
    QString codeset = match.captured(2);
    if (codeset.compare("utf8", Qt::CaseInsensitive) == 0 || codeset.compare("utf-8", Qt::CaseInsensitive) == 0)
        codeset = "UTF-8";
    return match.captured(1) + (codeset.isEmpty() ? QString() : "." + codeset) + match.captured(3);
}
QLocale qtLocale(const QString &name)
{
    return QLocale(name.section('.', 0, 0).section('@', 0, 0));
}
QString unquoted(QString value)
{
    value = value.trimmed();
    if (value.size() >= 2 && (value.startsWith('"') || value.startsWith('\'')) && value.endsWith(value.front()))
        value = value.mid(1, value.size() - 2);
    return value;
}
QString keyOf(const QString &line)
{
    static const QRegularExpression assignment("^\\s*(?:export\\s+)?([A-Z_]+)=");
    const auto match = assignment.match(line);
    return match.hasMatch() ? match.captured(1) : QString();
}
} // namespace

LocaleSettings::LocaleSettings(QString file, QStringList installed, QProcessEnvironment environment, QObject *parent)
    : QObject(parent), m_file(std::move(file))
{
    for (const auto &name : installed)
        if (!m_installed.contains(normalized(name)))
            m_installed.append(normalized(name));
    m_lang = normalized(environment.value("LANG"));
    const auto started = [&](const char *key)
    {
        for (const auto &name : {QString("LC_ALL"), QString(key), QString("LANG")})
            if (!environment.value(name).isEmpty())
                return normalized(environment.value(name));
        return QString("C");
    };
    m_startedNumbers = started("LC_NUMERIC");
    m_startedText = started("LC_COLLATE");
    refresh();
}
QString LocaleSettings::standardFile(const QProcessEnvironment &environment)
{
    const auto config = environment.value("XDG_CONFIG_HOME");
    return (config.isEmpty() ? environment.value("HOME") + "/.config" : config) + "/locale.conf";
}
QStringList LocaleSettings::installedLocales()
{
    QProcess process;
    process.start("locale", {"-a"});
    if (!process.waitForFinished(3000))
        return {};
    QStringList result;
    for (const auto &line : QString::fromLocal8Bit(process.readAllStandardOutput()).split('\n', Qt::SkipEmptyParts))
        if (line.contains('_') && normalized(line).section('@', 0, 0).endsWith(".UTF-8"))
            result.append(normalized(line));
    return result;
}
void LocaleSettings::refresh()
{
    QFile file(m_file);
    m_lines = file.open(QIODevice::ReadOnly) ? QString::fromUtf8(file.readAll()).split('\n') : QStringList();
    while (!m_lines.isEmpty() && m_lines.last().isEmpty())
        m_lines.removeLast();
    emit changed();
}
QString LocaleSettings::value(const QString &key) const
{
    QString found;
    for (const auto &line : m_lines)
        if (keyOf(line) == key)
            found = normalized(unquoted(line.section('=', 1)));
    return found;
}
QVariantList LocaleSettings::formats() const
{
    QVariantList result;
    for (const auto &name : m_installed)
    {
        const auto locale = qtLocale(name);
        result.append(QVariantMap{{"name", name}, {"region", QLocale::territoryToString(locale.territory())},
                                  {"language", QLocale::languageToString(locale.language())},
                                  {"script", QLocale::scriptToString(locale.script())}});
    }
    std::sort(result.begin(), result.end(), [](const QVariant &a, const QVariant &b)
              { return a.toMap().value("region").toString() < b.toMap().value("region").toString(); });
    return result;
}
QString LocaleSettings::numbers() const
{
    for (const auto &key : {"LC_NUMERIC", "LANG"})
        if (const auto saved = value(key); !saved.isEmpty())
            return saved;
    return m_lang.isEmpty() ? QString("C") : m_lang;
}
QString LocaleSettings::text() const
{
    for (const auto &key : {"LC_COLLATE", "LANG"})
        if (const auto saved = value(key); !saved.isEmpty())
            return saved;
    return m_lang.isEmpty() ? QString("C") : m_lang;
}
QVariantMap LocaleSettings::numberFormat(const QString &name) const
{
    const auto locale = qtLocale(name);
    const auto symbol = locale.currencySymbol(QLocale::CurrencySymbol);
    const auto sample = locale.toCurrencyString(1234.56);
    const auto digit = sample.indexOf(QRegularExpression("\\d"));
    return {{"decimal", locale.decimalPoint()}, {"thousands", locale.groupSeparator()}, {"symbol", symbol},
            {"before", !symbol.isEmpty() && sample.indexOf(symbol) >= 0 && sample.indexOf(symbol) < digit},
            {"sample", sample}};
}
bool LocaleSettings::setNumbers(const QString &locale) { return save({"LC_NUMERIC", "LC_MONETARY"}, locale); }
bool LocaleSettings::setText(const QString &locale) { return save({"LC_COLLATE", "LC_CTYPE"}, locale); }
bool LocaleSettings::save(const QStringList &keys, const QString &requested)
{
    const auto locale = normalized(requested);
    if (!m_installed.contains(locale))
    {
        m_error = locale + " is not installed on this computer.";
        emit changed();
        return false;
    }
    auto lines = m_lines;
    // The login reads this file instead of /etc/locale.conf: keep the language it would have had.
    QString lang = value("LANG");
    if (lang.isEmpty())
    {
        lang = m_lang.isEmpty() ? QString("C.UTF-8") : m_lang;
        lines.prepend("LANG=" + lang);
    }
    lines.erase(std::remove_if(lines.begin(), lines.end(), [&](const QString &line) { return keys.contains(keyOf(line)); }),
                lines.end());
    if (locale != lang)
        for (const auto &key : keys)
            lines.append(key + "=" + locale);
    QDir().mkpath(QFileInfo(m_file).absolutePath());
    QSaveFile file(m_file);
    if (!file.open(QIODevice::WriteOnly) || file.write((lines.join('\n') + '\n').toUtf8()) < 0 || !file.commit())
    {
        m_error = "The setting could not be saved in " + m_file + ".";
        emit changed();
        return false;
    }
    m_error.clear();
    m_lines = lines;
    emit changed();
    return true;
}
