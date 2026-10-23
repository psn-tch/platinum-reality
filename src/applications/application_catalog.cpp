#include "application_catalog.hpp"
#include <QCoreApplication>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QFutureWatcher>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMimeDatabase>
#include <QSaveFile>
#include <QSet>
#include <QtConcurrent>
#include <algorithm>

using namespace frankenstein::applications;
namespace
{
constexpr auto editorId = "org.frankenstein.Editor.desktop";
constexpr auto bluetoothId = "org.frankenstein.Bluetooth.desktop";
QString panelAction(const DesktopEntry &entry)
{
    return entry.filename.isEmpty() && entry.id == bluetoothId ? QString("bluetooth") : QString();
}
QVector<DesktopEntry> builtins(const QString &workspace, const QString &profile,
                               const QString &binaryDirectory)
{
    auto literal = [](QString value) { return value.replace('%', "%%"); };
    DesktopEntry editor;
    editor.id = editorId;
    editor.name = "Editor";
    editor.comment = "Create and edit text documents.";
    editor.tokens = {literal(binaryDirectory + "/frankenstein-editor"), "%f"};
    editor.documentCode = 'f';
    editor.mimeTypes = {"text/plain", "application/json", "application/xml", "application/x-desktop"};
    DesktopEntry finder;
    finder.id = "org.frankenstein.Finder.desktop";
    finder.name = "Finder";
    finder.comment = "Open your hard disk and its folder windows.";
    finder.tokens = {literal(binaryDirectory + "/frankenstein-finder"), "--root", literal(workspace),
                      "--profile", literal(profile)};
    DesktopEntry bluetooth;
    bluetooth.id = bluetoothId;
    bluetooth.name = "Bluetooth";
    bluetooth.comment = "Connect, pair and manage Bluetooth devices.";
    bluetooth.keywords = {"Control Panel", "wireless", "headphones", "keyboard", "mouse"};
    return {editor, finder, bluetooth};
}
struct Scan
{
    QVector<DesktopEntry> entries;
    QString notice;
};
Scan scanApplications(const QStringList &directories, QVector<DesktopEntry> entries,
                        const QProcessEnvironment &environment)
{
    Scan result{std::move(entries), {}};
    QSet<QString> seen;
    for (const auto &entry : result.entries)
        seen.insert(entry.id);
    qsizetype visited = 0;
    qint64 bytes = 0;
    for (const auto &directory : directories)
    {
        QStringList paths;
        QDirIterator iterator(directory, {"*.desktop"}, QDir::Files | QDir::AllDirs | QDir::NoDotAndDotDot,
                              QDirIterator::Subdirectories);
        while (iterator.hasNext())
        {
            if (++visited > 8192)
            {
                result.notice = "The application list reached its size limit.";
                break;
            }
            const auto path = iterator.next();
            if (iterator.fileInfo().isFile()) paths.push_back(path);
        }
        paths.sort();
        for (const auto &path : paths)
        {
            auto id = QDir(directory).relativeFilePath(path).replace('/', '-');
            if (seen.contains(id))
                continue;
            seen.insert(id); // A hidden/invalid user entry still masks a lower-priority entry.
            if (bytes >= 64 * 1024 * 1024 || result.entries.size() >= 4096)
            {
                result.notice = "The application list reached its size limit.";
                break;
            }
            auto entry = readDesktopEntry(path, id, environment);
            bytes += entry.sourceBytes;
            if (!entry.hidden && !entry.name.isEmpty())
                result.entries.push_back(std::move(entry));
        }
        if (!result.notice.isEmpty())
            break;
    }
    std::sort(result.entries.begin(), result.entries.end(), [](const auto &a, const auto &b)
    {
        const int order = QString::localeAwareCompare(a.name, b.name);
        return order == 0 ? a.id < b.id : order < 0;
    });
    return result;
}
} // namespace

ApplicationCatalog::ApplicationCatalog(QStringList directories, QString workspace,
    QString profile, QString binaryDirectory, QObject *parent)
    : QAbstractListModel(parent), m_directories(std::move(directories)),
      m_workspace(QDir(workspace).absolutePath()), m_profile(QDir(profile).absolutePath()),
      m_binaryDirectory(std::move(binaryDirectory)),
      m_entries(builtins(m_workspace, m_profile, m_binaryDirectory))
{
    QFile input(m_profile + "/associations.json");
    if (input.open(QIODevice::ReadOnly) && input.size() <= 128 * 1024)
    {
        const auto object = QJsonDocument::fromJson(input.read(128 * 1024 + 1)).object();
        if (object.value("version").toInt() == 1)
        {
            const auto types = object.value("types").toObject();
            for (auto it = types.begin(); it != types.end() && m_associations.size() < 1024; ++it)
                if (it.key().size() <= 256 && it.value().isString() && it.value().toString().size() <= 1024)
                    m_associations[it.key()] = it.value().toString();
        }
    }
    filter();
}
int ApplicationCatalog::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : count();
}
QHash<int, QByteArray> ApplicationCatalog::roleNames() const
{
    return {{IdRole, "applicationId"}, {NameRole, "displayName"}, {DescriptionRole, "description"},
             {LaunchableRole, "launchable"}, {ReasonRole, "unavailableReason"}};
}
QVariant ApplicationCatalog::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= count())
        return {};
    const auto &entry = m_entries[m_visible[index.row()]];
    switch (role)
    {
    case IdRole: return entry.id;
    case NameRole: return entry.name;
    case DescriptionRole: return entry.comment;
    case LaunchableRole: return reason(entry).isEmpty();
    case ReasonRole: return reason(entry);
    default: return {};
    }
}
QVariantMap ApplicationCatalog::item(int row) const
{
    if (row < 0 || row >= count())
        return {};
    return {{"id", data(index(row), IdRole)}, {"name", data(index(row), NameRole)},
             {"description", data(index(row), DescriptionRole)},
             {"launchable", data(index(row), LaunchableRole)},
             {"reason", data(index(row), ReasonRole)}};
}
void ApplicationCatalog::setQuery(QString query)
{
    if (m_query == query) return;
    m_query = std::move(query); filter(); emit filterChanged();
}
void ApplicationCatalog::setDocument(QString document)
{
    if (m_document == document) return;
    m_document = std::move(document);
    m_documentType = m_document.isEmpty() ? QMimeType() : QMimeDatabase().mimeTypeForFile(m_document);
    filter(); emit filterChanged();
}
void ApplicationCatalog::setShowAll(bool value)
{
    if (m_showAll == value) return;
    m_showAll = value; filter(); emit filterChanged();
}
QString ApplicationCatalog::reason(const DesktopEntry &entry) const
{
    if (!panelAction(entry).isEmpty())
        return m_document.isEmpty() ? QString() : QString("Control panels cannot open documents.");
    return planLaunch(entry, m_document.isEmpty() ? QStringList{} : QStringList{m_document},
                        m_workspace, QProcessEnvironment::systemEnvironment()).error;
}
bool ApplicationCatalog::matchesDocument(const DesktopEntry &entry) const
{
    if (m_document.isEmpty()) return !entry.noDisplay;
    if (m_document.endsWith(".desktop", Qt::CaseInsensitive)) return entry.id == editorId;
    if (!entry.documentCode) return false;
    if (m_showAll) return true;
    return std::any_of(entry.mimeTypes.begin(), entry.mimeTypes.end(), [&](const auto &name)
                       { return m_documentType.name() == name || m_documentType.inherits(name); });
}
void ApplicationCatalog::filter()
{
    beginResetModel();
    m_visible.clear();
    const auto terms = m_query.simplified().split(' ', Qt::SkipEmptyParts);
    for (int i = 0; i < m_entries.size(); ++i)
    {
        const auto &entry = m_entries[i];
        const auto searchable = entry.name + " " + entry.genericName + " " + entry.comment + " " + entry.keywords.join(' ');
        if (entry.showHere && matchesDocument(entry) &&
            std::all_of(terms.begin(), terms.end(), [&](const auto &term)
                        { return searchable.contains(term, Qt::CaseInsensitive); }))
            m_visible.push_back(i);
    }
    endResetModel();
    emit countChanged();
}
void ApplicationCatalog::refresh()
{
    if (m_busy) return;
    m_busy = true; emit stateChanged();
    auto *watcher = new QFutureWatcher<Scan>(this);
    connect(watcher, &QFutureWatcher<Scan>::finished, this, [this, watcher]
    {
        auto result = watcher->result();
        watcher->deleteLater();
        m_entries = std::move(result.entries);
        m_notice = std::move(result.notice);
        m_busy = false; m_ready = true;
        filter(); emit stateChanged(); emit refreshed();
    });
    watcher->setFuture(QtConcurrent::run(scanApplications, m_directories,
        builtins(m_workspace, m_profile, m_binaryDirectory), QProcessEnvironment::systemEnvironment()));
}
const DesktopEntry *ApplicationCatalog::find(const QString &id) const
{
    for (const auto &entry : m_entries)
        if (entry.id == id) return &entry;
    return nullptr;
}
LaunchPlan ApplicationCatalog::launchPlan(const QString &id, const QStringList &documents,
                                           const QProcessEnvironment &environment) const
{
    const auto *entry = find(id);
    if (!entry || entry->hidden || !entry->showHere)
        return {{}, {}, {}, "This application is no longer available. Refresh the list."};
    if (!panelAction(*entry).isEmpty())
        return {{}, {}, {}, "This control panel opens inside the desktop and cannot open documents."};
    if (!entry->filename.isEmpty())
    {
        const auto current = readDesktopEntry(entry->filename, id, environment);
        if (current.fingerprint != entry->fingerprint)
            return {{}, {}, {}, "This application entry changed. Refresh the list before opening it."};
    }
    return planLaunch(*entry, documents, m_workspace, environment);
}
QString ApplicationCatalog::builtInAction(const QString &id) const
{
    const auto *entry = find(id);
    return entry && !entry->hidden && entry->showHere ? panelAction(*entry) : QString();
}
QString ApplicationCatalog::preferredApplication(const QString &path) const
{
    // A workspace desktop entry is a document, never an executable shortcut.
    if (path.endsWith(".desktop", Qt::CaseInsensitive))
        return editorId;
    const auto mime = QMimeDatabase().mimeTypeForFile(path);
    const auto preferred = m_associations.value(mime.name());
    if (const auto *entry = find(preferred); entry && entry->documentCode && entry->showHere)
        return preferred;
    if (mime.inherits("text/plain"))
        return editorId;
    return {};
}
bool ApplicationCatalog::rememberApplication(const QString &id, const QString &path, QString &error)
{
    const auto *entry = find(id);
    if (!entry || !entry->documentCode || !entry->showHere || !entry->unavailable.isEmpty())
    {
        error = "This application cannot be remembered for documents.";
        return false;
    }
    auto next = m_associations;
    next[QMimeDatabase().mimeTypeForFile(path).name()] = id;
    QJsonObject types;
    for (auto it = next.begin(); it != next.end(); ++it) types[it.key()] = it.value();
    QSaveFile output(m_profile + "/associations.json");
    const auto bytes = QJsonDocument(QJsonObject{{"version", 1}, {"types", types}}).toJson();
    if (!output.open(QIODevice::WriteOnly) || output.write(bytes) != bytes.size() || !output.commit())
    {
        error = "The application choice could not be saved: " + output.errorString();
        return false;
    }
    m_associations = std::move(next);
    return true;
}
QString ApplicationCatalog::applicationName(const QString &waylandId) const
{
    const auto *entry = find(waylandId.endsWith(".desktop") ? waylandId : waylandId + ".desktop");
    return entry ? entry->name : QString();
}
