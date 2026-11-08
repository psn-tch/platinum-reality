#include "printer_selector.hpp"

#include <QProcess>
#include <QRegularExpression>
#include <QVariantMap>

PrinterSelector::PrinterSelector(QString lpstat, QString lpoptions, QObject *parent)
    : QObject(parent), m_lpstat(std::move(lpstat)), m_lpoptions(std::move(lpoptions))
{
}

QVariantList PrinterSelector::printers() const
{
    QVariantList result;
    for (const auto &name : m_names)
        result.append(QVariantMap{{"name", name}, {"default", name == m_default}});
    return result;
}

void PrinterSelector::run(const QString &program, const QStringList &arguments,
                          std::function<void(int, const QByteArray &)> done)
{
    auto *process = new QProcess(this);
    m_running = process;
    process->setProcessChannelMode(QProcess::SeparateChannels);
    connect(process, &QProcess::finished, this, [this, process, done](int code, QProcess::ExitStatus status) {
        const auto output = process->readAllStandardOutput();
        m_running = nullptr;
        process->deleteLater();
        done(status == QProcess::NormalExit ? code : -1, output);
    });
    connect(process, &QProcess::errorOccurred, this, [this, process, done](QProcess::ProcessError error) {
        if (error != QProcess::FailedToStart)
            return;
        m_running = nullptr;
        process->deleteLater();
        done(-1, {});
    });
    process->start(program, arguments);
    emit changed();
}

void PrinterSelector::refresh()
{
    if (m_running)
        return;
    // lpstat -e: one destination name per line. lpstat -d: "system default destination: NAME"
    // (or a user's lpoptions default, which lpstat reports the same way).
    run(m_lpstat, {"-e"}, [this](int code, const QByteArray &output) {
        QStringList names;
        if (code == 0)
            for (const auto &line : QString::fromUtf8(output).split('\n', Qt::SkipEmptyParts))
                if (!line.trimmed().isEmpty() && !line.contains(' '))
                    names.append(line.trimmed());
        m_names = names;
        run(m_lpstat, {"-d"}, [this](int code, const QByteArray &output) {
            static const QRegularExpression pattern("destination:\\s*(\\S+)");
            const auto match = pattern.match(QString::fromUtf8(output));
            m_default = code == 0 && match.hasMatch() ? match.captured(1) : QString();
            emit changed();
        });
    });
}

bool PrinterSelector::setDefault(const QString &name)
{
    if (m_running || !m_names.contains(name))
        return false;
    run(m_lpoptions, {"-d", name}, [this](int, const QByteArray &) { refresh(); });
    return true;
}
