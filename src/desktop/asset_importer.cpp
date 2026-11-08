#include "asset_importer.hpp"
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcessEnvironment>

AssetImporter::AssetImporter(QString helper, QString assets, QObject *parent)
    : QObject(parent), m_helper(std::move(helper)), m_assets(std::move(assets))
{
    m_timeout.setSingleShot(true);
    connect(&m_timeout, &QTimer::timeout, this, [this] {
        m_timedOut = true;
        m_process.kill();
    });
    connect(&m_process, &QProcess::errorOccurred, this, [this](QProcess::ProcessError e) {
        if (e != QProcess::FailedToStart) return;
        m_timeout.stop(); m_busy = false;
        m_error = "The importer could not start.";
        emit busyChanged(); emit finished();
    });
    connect(&m_process, &QProcess::finished, this, [this](int code, QProcess::ExitStatus status) {
        m_timeout.stop();
        const auto output = m_process.readAllStandardOutput();
        const auto problem = QString::fromUtf8(m_process.readAllStandardError()).trimmed();
        const auto document = output.size() <= (1 << 20) ? QJsonDocument::fromJson(output) : QJsonDocument{};
        const auto object = document.object();
        const bool finding = m_finding;
        m_busy = false;
        if (finding) {
            m_sources = object.value("sources").toArray().toVariantList();
            emit busyChanged(); emit sourcesChanged();
            return;
        }
        m_error.clear();
        if (m_timedOut)
            m_error = "The import took too long and was stopped.";
        else if (status != QProcess::NormalExit || code != 0 || !document.isObject()) {
            // The helper says why in its last line (SystemExit), e.g. no System Folder on that disk.
            const auto last = problem.split('\n').last().trimmed();
            m_error = last.isEmpty() || last.startsWith("Traceback") ? QString("The import could not finish.") : last;
            if (m_error.contains("lock"))
                m_error = "That disk is in use. Quit the Mac OS 9 virtual machine, then try again.";
        }
        m_sections = m_error.isEmpty() ? object.value("sections").toArray().toVariantList() : QVariantList{};
        emit busyChanged(); emit finished();
    });
}

AssetImporter::~AssetImporter()
{
    m_process.disconnect(this);
    if (m_process.state() != QProcess::NotRunning) {
        m_process.kill();
        m_process.waitForFinished(1000);
    }
}

void AssetImporter::run(const QStringList &arguments, bool finding)
{
    m_finding = finding; m_timedOut = false; m_busy = true;
    auto environment = QProcessEnvironment::systemEnvironment();
    // Do not import code from an inherited Python search path.
    environment.remove("PYTHONPATH"); environment.remove("PYTHONHOME");
    environment.insert("PYTHONDONTWRITEBYTECODE", "1");
    m_process.setProcessEnvironment(environment);
    m_process.start("/usr/bin/python3", QStringList{"-I", m_helper} + arguments);
    // Converting a large virtual disk can take a while; finding sources is a directory walk.
    m_timeout.start(finding ? 60000 : 15 * 60000);
    emit busyChanged();
}

void AssetImporter::findSources()
{
    if (m_busy || !QFileInfo(m_helper).isFile()) return;
    run(m_roots.isEmpty() ? QStringList{"--find"} : QStringList{"--find", "--roots"} + m_roots, true);
}

bool AssetImporter::importFrom(const QString &path, bool replace)
{
    if (m_busy) return false;
    if (!QFileInfo(m_helper).isFile() || !QFileInfo(m_helper).isAbsolute()) {
        m_error = "The importer is not installed."; m_sections.clear();
        emit finished(); return false;
    }
    const QFileInfo source(path);
    if (path.isEmpty() || !source.isAbsolute() || !source.exists()) {
        m_error = "Choose a Mac OS 9 disk, CD image or System Folder that exists."; m_sections.clear();
        emit finished(); return false;
    }
    m_sections.clear(); m_error.clear();
    QStringList arguments{source.absoluteFilePath(), "--json", "--assets", m_assets};
    if (replace) arguments << "--replace";
    run(arguments, false);
    return true;
}
