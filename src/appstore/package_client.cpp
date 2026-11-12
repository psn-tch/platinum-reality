#include "package_client.hpp"
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcessEnvironment>

PackageClient::PackageClient(QString helper, QObject *parent) : QObject(parent), m_helper(std::move(helper))
{
    auto environment = QProcessEnvironment::systemEnvironment();
    environment.remove("PYTHONPATH");
    environment.remove("PYTHONHOME");
    environment.insert("PYTHONDONTWRITEBYTECODE", "1");
    environment.insert("LC_ALL", "C.UTF-8");
    for (auto *process : {&m_catalog, &m_change})
        process->setProcessEnvironment(environment);
    m_catalog.setStandardErrorFile(QProcess::nullDevice());
    // makepkg and yay write some of their progress to stderr; it is part of the story.
    m_change.setProcessChannelMode(QProcess::MergedChannels);
    connect(&m_catalog, &QProcess::finished, this, [this](int code, QProcess::ExitStatus status) {
        const auto output = m_catalog.readAllStandardOutput();
        if (m_reload)
        {
            // The software changed while this was being read: read it again.
            m_reload = false;
            m_catalog.start(m_helper, {"catalog"});
            return;
        }
        QJsonParseError problem;
        const auto document = QJsonDocument::fromJson(output, &problem);
        if (status != QProcess::NormalExit || code != 0 || !document.isObject())
        {
            const auto message = QJsonDocument::fromJson(output.trimmed().split('\n').last()).object();
            emit catalogFailed(message.value("error").toString("The software lists could not be read."));
            return;
        }
        emit catalogLoaded(Catalog::fromJson(document.object()));
    });
    connect(&m_catalog, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart)
            emit catalogFailed("The App Store's helper is missing. Reinstall Platinum Reality.");
    });
    connect(&m_change, &QProcess::readyReadStandardOutput, this, &PackageClient::readChange);
    connect(&m_change, &QProcess::finished, this, [this](int code, QProcess::ExitStatus status) {
        readChange();
        const bool ok = status == QProcess::NormalExit && code == 0 && m_error.isEmpty();
        if (!ok && m_error.isEmpty() && !m_cancelled)
            m_error = "The change could not be made.";
        emit finished(ok, m_error, m_cancelled);
    });
    connect(&m_change, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart)
            emit finished(false, "The App Store's helper is missing. Reinstall Platinum Reality.", false);
    });
}

PackageClient::~PackageClient()
{
    m_catalog.disconnect(this);
    m_change.disconnect(this);
    m_catalog.kill();
    m_catalog.waitForFinished(2000);
    // pacman must never be stopped halfway; the window doesn't close while a change runs.
    m_change.waitForFinished(-1);
}

void PackageClient::loadCatalog()
{
    if (loading())
    {
        m_reload = true;
        return;
    }
    m_catalog.start(m_helper, {"catalog"});
}

bool PackageClient::run(const QStringList &arguments)
{
    if (busy())
        return false;
    m_pending.clear();
    m_error.clear();
    m_cancelled = false;
    m_touched.clear();
    m_change.start(m_helper, arguments);
    return true;
}

void PackageClient::readChange()
{
    m_pending += m_change.readAllStandardOutput();
    int end;
    while ((end = m_pending.indexOf('\n')) >= 0)
    {
        const auto line = m_pending.left(end).trimmed();
        m_pending.remove(0, end + 1);
        const auto message = QJsonDocument::fromJson(line).object();
        if (message.isEmpty())
        {
            // Not one of the helper's lines (yay and makepkg talk directly): show it as it is.
            if (!line.isEmpty())
            {
                emit output(QString::fromUtf8(line));
                emit progress(QString::fromUtf8(line), -1, -1);
            }
            continue;
        }
        if (message.contains("error"))
        {
            m_error = message.value("error").toString();
            emit output("error: " + m_error);
        }
        else if (message.value("cancelled").toBool())
        {
            m_cancelled = true;
            emit output("Cancelled: the password wasn’t given.");
        }
        else if (message.contains("step"))
        {
            static const QHash<QString, QString> verbs{{"installing", "Installing"}, {"upgrading", "Updating"},
                                                       {"reinstalling", "Reinstalling"},
                                                       {"downgrading", "Installing"}, {"removing", "Removing"}};
            const auto name = message.value("package").toString();
            m_touched.append(name);
            const int step = message.value("step").toInt(), steps = message.value("steps").toInt();
            emit output(message.value("line").toString(
                QString("(%1/%2) %3 %4").arg(message.value("step").toInt()).arg(message.value("steps").toInt())
                    .arg(message.value("doing").toString(), name)));
            emit progress(QString("%1 %2 (%3 of %4)…")
                              .arg(verbs.value(message.value("doing").toString(), "Installing"), name)
                              .arg(step)
                              .arg(steps),
                          step, steps);
        }
        else if (message.contains("progress"))
        {
            emit output(message.value("progress").toString());
            emit progress(message.value("progress").toString(), -1, -1);
        }
    }
}
