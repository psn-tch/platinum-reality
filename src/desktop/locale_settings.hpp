#pragma once
#include <QObject>
#include <QProcessEnvironment>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>

// Numbers and Text (Lily Hewitt, 2026-11-15), after 9.2.2's control panels (captured 2026-11-12).
// On Linux a number format is the LC_NUMERIC and LC_MONETARY locale, and a text behavior is the
// LC_COLLATE and LC_CTYPE locale (sort order, case conversion, which characters are letters).
// The choices are the installed locales (`locale -a`; Arch adds more with locale-gen, as root).
// They are saved in the standard $XDG_CONFIG_HOME/locale.conf, which Arch's login shell reads
// instead of /etc/locale.conf, so the file always keeps LANG too. Programs read their locale when
// they start, so a change takes effect for programs opened after the next login.
class LocaleSettings final : public QObject
{
    Q_OBJECT
    // {name, region, language, script}: name as in locale.conf (en_US.UTF-8); the rest in English.
    Q_PROPERTY(QVariantList formats READ formats NOTIFY changed)
    // The saved choices: what programs get at the next login.
    Q_PROPERTY(QString numbers READ numbers NOTIFY changed)
    Q_PROPERTY(QString text READ text NOTIFY changed)
    // What this session started with.
    Q_PROPERTY(QString startedNumbers READ startedNumbers CONSTANT)
    Q_PROPERTY(QString startedText READ startedText CONSTANT)
    Q_PROPERTY(QString error READ error NOTIFY changed)
  public:
    // file: the locale.conf to read and write; installed: the locales to offer (any spelling of
    // .UTF-8); environment: this session's, for LANG and what it started with.
    LocaleSettings(QString file, QStringList installed, QProcessEnvironment environment, QObject *parent = nullptr);
    // $XDG_CONFIG_HOME/locale.conf, else ~/.config/locale.conf.
    static QString standardFile(const QProcessEnvironment &environment);
    // The UTF-8 locales `locale -a` lists, such as en_US.UTF-8 (not C or POSIX).
    static QStringList installedLocales();
    QVariantList formats() const;
    QString numbers() const;
    QString text() const;
    QString startedNumbers() const { return m_startedNumbers; }
    QString startedText() const { return m_startedText; }
    QString error() const { return m_error; }
    // {decimal, thousands, symbol, before, sample}: how the locale writes numbers and money.
    Q_INVOKABLE QVariantMap numberFormat(const QString &locale) const;
    // Saves the choice for LC_NUMERIC and LC_MONETARY, or LC_COLLATE and LC_CTYPE; false if the
    // locale is not installed or the file could not be written (error says which).
    Q_INVOKABLE bool setNumbers(const QString &locale);
    Q_INVOKABLE bool setText(const QString &locale);
    Q_INVOKABLE void refresh();
  signals:
    void changed();

  private:
    QString value(const QString &key) const;
    bool save(const QStringList &keys, const QString &locale);
    QString m_file;
    QStringList m_installed;
    QString m_lang, m_startedNumbers, m_startedText, m_error;
    QStringList m_lines; // The file as read, comments and other settings kept as they are.
};
