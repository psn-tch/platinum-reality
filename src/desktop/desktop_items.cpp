#include "desktop_items.hpp"
#include "applications/desktop_entry.hpp"
#include <QProcessEnvironment>
#include <unistd.h>
#include <QCollator>
#include "finder/file_details.hpp"
#include <QDirIterator>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFutureWatcher>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRect>
#include <QRegion>
#include <QSaveFile>
#include <QtConcurrent>
#include <algorithm>
#include <fcntl.h>
#include <limits>
#include <sys/stat.h>
#include <utility>

namespace
{
constexpr int itemWidth = 100, itemHeight = 76, topMargin = 40;
constexpr int maximumEntries = 4096;
struct OperationResult
{
    QStringList completed;
    QString error;
};

QString entryKey(const QFileInfo &file)
{
    // Birth time disambiguates inode reuse. statx does not follow symlinks.
    // Without filesystem birth-time support, use the name and make no stable-
    // identity claim. This is scoped to one desktop directory, not a portable ID.
    struct statx info{};
    const auto path = QFile::encodeName(file.absoluteFilePath());
    if (::statx(AT_FDCWD, path.constData(), AT_SYMLINK_NOFOLLOW,
                STATX_INO | STATX_BTIME | STATX_NLINK, &info) == 0 &&
        (info.stx_mask & STATX_BTIME))
    {
        auto key = QString("file:%1:%2:%3:%4:%5")
                       .arg(info.stx_dev_major)
                       .arg(info.stx_dev_minor)
                       .arg(info.stx_ino)
                       .arg(info.stx_btime.tv_sec)
                       .arg(info.stx_btime.tv_nsec);
        if (info.stx_nlink > 1 && !file.isDir())
            key += ":" + file.fileName();
        return key;
    }
    return "name:" + file.fileName();
}
} // namespace

DesktopItems::DesktopItems(QString workspace, QString profile, QObject *parent, QSize viewport)
    : QAbstractListModel(parent), m_root(QDir(workspace).absolutePath()),
      m_profile(std::move(profile)), m_directory(m_root + "/Desktop"),
      m_operations(m_root, FinderPlaces::trashFor(m_profile), FinderPlaces::volumes),
      m_viewport(std::max(itemWidth, viewport.width()),
                 std::max(topMargin + itemHeight, viewport.height()))
{
    QDir().mkpath(m_profile);
    if (!QFileInfo::exists(m_directory))
        m_operations.createFolder(m_root, "Desktop");
    loadPositions();
    m_refresh.setSingleShot(true);
    m_refresh.setInterval(30);
    connect(&m_refresh, &QTimer::timeout, this, &DesktopItems::refresh);
    connect(&m_watcher, &QFileSystemWatcher::directoryChanged, this, [this] { m_refresh.start(); });
    // Drives come and go (the mount folder itself may not exist until the first one).
    m_drives.setInterval(3000);
    connect(&m_drives, &QTimer::timeout, this, [this]
    {
        QStringList names;
        for (const auto &volumes : FinderPlaces::volumes)
            for (const auto &drive : QDir(volumes).entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name))
                if (names.size() < 32 && m_operations.readableDirectory(drive.absoluteFilePath()))
                    names << drive.absoluteFilePath();
        if (names != m_driveNames)
            m_refresh.start();
    });
    if (!FinderPlaces::volumes.isEmpty())
        m_drives.start();
    m_watcher.addPath(m_root);
    refresh();
}
int DesktopItems::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : int(m_entries.size());
}
QVariant DesktopItems::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_entries.size())
        return {};
    const auto &entry = m_entries[index.row()];
    switch (role)
    {
    case KeyRole:
        return entry.key;
    case NameRole:
        return entry.name;
    case PathRole:
        return entry.path;
    case KindRole:
        return entry.kind;
    case XRole:
        return position(entry).x();
    case YRole:
        return position(entry).y();
    case SelectedRole:
        return m_selected.contains(entry.key);
    default:
        return {};
    }
}
QHash<int, QByteArray> DesktopItems::roleNames() const
{
    return {{KeyRole, "entryKey"},       {NameRole, "entryName"}, {PathRole, "entryPath"},
            {KindRole, "entryKind"},     {XRole, "itemX"},        {YRole, "itemY"},
            {SelectedRole, "isSelected"}};
}
QPoint DesktopItems::position(const Entry &entry) const
{
    const auto p = m_positions.value(entry.key);
    if (m_displayLayout)
    {
        if (!m_dragPositions.contains(entry.key))
            return fittedPosition(p);
        // Allow a moving group to cross a gap; settle onto visible work areas
        // on release rather than snapping on each intermediate mouse step.
        return {std::clamp(p.x(), 0, std::max(0, m_viewport.width() - itemWidth)),
                std::clamp(p.y(), 0, std::max(0, m_viewport.height() - itemHeight))};
    }
    return {std::clamp(p.x(), 0, std::max(0, m_viewport.width() - itemWidth)),
            std::clamp(p.y(), topMargin, std::max(topMargin, m_viewport.height() - itemHeight))};
}
void DesktopItems::setViewport(int width, int height)
{
    if (m_displayLayout)
        return; // A transient host configure must not replace the real screens.
    const QSize size(std::max(itemWidth, width), std::max(topMargin + itemHeight, height));
    if (size == m_viewport)
        return;
    cancelSelection();
    m_viewport = size;
    if (!m_entries.isEmpty())
        emit dataChanged(index(0), index(rowCount() - 1), {XRole, YRole});
}
QVector<QRect> DesktopItems::placementAreas() const
{
    if (!m_displayLayout)
        return {QRect(0, topMargin, m_viewport.width(), m_viewport.height() - topMargin)};
    const auto primary = m_displayLayout->primaryRect();
    QVector<QRect> result{primary.adjusted(0, topMargin, 0, 0)};
    if (m_displayLayout->mode() == DisplayLayout::Mode::Extend)
        for (const auto &output : m_displayLayout->outputs())
            if (output.logical != primary)
                result.append(output.logical.adjusted(0, 14, 0, 0));
    return result;
}
QPoint DesktopItems::fittedPosition(QPoint requested, QSize footprint) const
{
    const auto areas = placementAreas();
    QRegion visible;
    for (const auto &area : areas)
        visible += area;
    if (QRegion(QRect(requested, footprint)).subtracted(visible).isEmpty())
        return requested;
    const auto fits = [&](const QRect &area)
    { return area.width() >= footprint.width() && area.height() >= footprint.height(); };
    const bool canFit = std::any_of(areas.cbegin(), areas.cend(), fits);
    QPoint best;
    qint64 distance = std::numeric_limits<qint64>::max();
    for (const auto &area : areas)
    {
        if (canFit && !fits(area))
            continue;
        const QPoint candidate(std::clamp(requested.x(), area.x(), std::max(area.x(), area.x() + area.width() - footprint.width())),
                               std::clamp(requested.y(), area.y(), std::max(area.y(), area.y() + area.height() - footprint.height())));
        const auto delta = candidate - requested;
        const qint64 score = qint64(delta.x()) * delta.x() + qint64(delta.y()) * delta.y();
        if (score < distance)
        {
            best = candidate;
            distance = score;
        }
    }
    return best;
}
bool DesktopItems::acceptsPoint(int x, int y) const
{
    if (!m_displayLayout)
        return x >= 0 && x < m_viewport.width() && y >= 26 && y < m_viewport.height();
    const auto primary = m_displayLayout->primaryRect();
    if (QRect(primary.topLeft(), QSize(primary.width(), 26)).contains(x, y))
        return false;
    if (m_displayLayout->mode() == DisplayLayout::Mode::Mirror)
        return primary.contains(x, y);
    for (const auto &output : m_displayLayout->outputs())
        if (output.logical.contains(x, y))
            return true;
    return false;
}
void DesktopItems::setDisplayLayout(const DisplayLayout &layout)
{
    cancelDrag();
    cancelSelection();
    clearExternalDrag();
    // First topology replaces the constructor's placeholder canvas. Only its
    // automatic positions are new; retain loaded and explicitly moved icons.
    if (!m_displayLayout)
        for (const auto &key : std::as_const(m_automaticPositions))
            m_positions.remove(key);
    m_displayLayout = layout;
    m_viewport = layout.desktopBounds().size();
    arrangeMissing();
    if (!m_entries.isEmpty())
        emit dataChanged(index(0), index(rowCount() - 1), {XRole, YRole});
}
void DesktopItems::settlePlacement(const QSet<QString> &keys)
{
    if (!m_displayLayout || keys.isEmpty())
        return;
    QRegion visible;
    const auto areas = placementAreas();
    for (const auto &area : areas)
        visible += area;
    QRect bounds;
    bool fits = true;
    for (const auto &entry : m_entries)
        if (keys.contains(entry.key))
        {
            const QRect rectangle(m_positions.value(entry.key), QSize(itemWidth, itemHeight));
            bounds |= rectangle;
            fits = fits && QRegion(rectangle).subtracted(visible).isEmpty();
        }
    if (fits)
        return;
    // Retain a group's relative placement when it fits on a connected monitor.
    // Oversized groups are rescued individually and may need Clean Up.
    if (std::any_of(areas.cbegin(), areas.cend(), [&](const QRect &area)
                    { return area.width() >= bounds.width() && area.height() >= bounds.height(); }))
    {
        const auto delta = fittedPosition(bounds.topLeft(), bounds.size()) - bounds.topLeft();
        for (const auto &key : keys)
            m_positions[key] += delta;
    }
    else
        for (const auto &key : keys)
            m_positions[key] = fittedPosition(m_positions.value(key));
}
void DesktopItems::refresh()
{
    // File notifications can precede worker completion. Publish one coherent
    // result after the job, retaining the rename's presentation identity.
    if (m_busy)
        return;
    QVector<Entry> entries{{"volume", FinderPlaces::diskName(m_profile), m_root, "disk", {}},
                           {"trash", "Trash", FinderPlaces::trashFor(m_profile) + "/files", "trash", {}}};
    QSet<QString> keys{"volume", "trash"};
    const auto trash = FinderPlaces::trashFor(m_profile) + "/files";
    if (QDir().mkpath(trash) && !m_watcher.directories().contains(trash))
        m_watcher.addPath(trash);
    const bool full = !QDir(trash).isEmpty(QDir::AllEntries | QDir::Hidden | QDir::System | QDir::NoDotAndDotDot);
    if (full != m_trashFull)
    {
        m_trashFull = full;
        emit trashChanged();
    }
    if (m_operations.readableDirectory(m_directory))
    {
        if (!m_watcher.directories().contains(m_directory))
            m_watcher.addPath(m_directory);
        const auto files = QDir(m_directory)
                               .entryInfoList(QDir::AllEntries | QDir::NoDotAndDotDot,
                                              QDir::Name | QDir::IgnoreCase);
        int accepted = 0;
        for (const auto &file : files)
        {
            if (accepted++ >= maximumEntries)
            {
                emit errorOccurred("Desktop contains more than 4096 visible entries. Open the "
                                   "Desktop folder to see all files.");
                break;
            }
            QString kind = file.isSymLink() ? "link" : file.isDir() ? "folder" : "file";
            auto name = file.fileName();
            // A launcher (KDE's rule for desktop files): the user's own executable .desktop file
            // shows its Name and opens its application. Any other .desktop file stays an inert file.
            if (trustedLauncher(file))
            {
                const auto launcher = frankenstein::applications::readDesktopEntry(
                    file.absoluteFilePath(), file.fileName(), QProcessEnvironment::systemEnvironment());
                if (!launcher.name.isEmpty())
                {
                    kind = "launcher";
                    name = launcher.name.left(120);
                }
            }
            entries.push_back({{}, name, file.absoluteFilePath(), kind, entryKey(file)});
        }
    }
    // Mounted drives, as Mac OS 9 showed every disk on the desktop (checked again by m_drives).
    m_driveNames.clear();
    for (const auto &volumes : FinderPlaces::volumes)
        for (const auto &drive : QDir(volumes).entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name))
            if (m_driveNames.size() < 32 && m_operations.readableDirectory(drive.absoluteFilePath()))
            {
                m_driveNames << drive.absoluteFilePath();
                const auto key = "drive:" + drive.absoluteFilePath();
                keys.insert(key);
                entries.push_back({key, drive.fileName().left(120), drive.absoluteFilePath(), "disk", {}});
            }
    QMap<QString, QString> byName, byIdentity;
    for (const auto &entry : m_rememberedEntries)
        if (!entry.filesystemIdentity.isEmpty())
        {
            byName[entry.name] = entry.key;
            byIdentity[entry.filesystemIdentity] = entry.key;
        }
    // Identity matches take precedence, so a rename plus a new file at the old
    // name cannot assign the same presentation identity twice.
    for (auto &entry : entries)
        if (entry.key.isEmpty() && byIdentity.contains(entry.filesystemIdentity))
        {
            const auto previous = byIdentity.value(entry.filesystemIdentity);
            if (!keys.contains(previous))
            {
                entry.key = previous;
                keys.insert(previous);
            }
        }
    for (auto &entry : entries)
        if (entry.key.isEmpty())
        {
            const auto previous = byName.value(entry.name);
            entry.key = !previous.isEmpty() && !keys.contains(previous) ? previous
                                                                        : entry.filesystemIdentity;
            // Names preserve placement across atomic replacement, not authority
            // to open a file: every open is checked again by the manager.
            while (keys.contains(entry.key))
                entry.key += ":" + entry.name;
            keys.insert(entry.key);
        }
    if (entries == m_entries)
        return; // Repeated notifications must not cancel a live drag.
    cancelDrag();
    cancelSelection();
    const auto previousSelection = m_selected;
    beginResetModel();
    m_automaticPositions.intersect(keys);
    for (auto it = m_positions.begin(); it != m_positions.end();)
        if (!keys.contains(it.key()))
            it = m_positions.erase(it);
        else
            ++it;
    m_rememberedEntries = entries;
    m_entries = std::move(entries);
    m_selected.intersect(keys);
    arrangeMissing();
    endResetModel();
    if (m_selected != previousSelection)
        emit selectionChanged();
}
void DesktopItems::arrangeMissing()
{
    if (m_displayLayout)
    {
        const auto areas = placementAreas();
        const auto primary = areas.first();
        const int right = std::max(primary.x(), primary.x() + primary.width() - itemWidth - 20);
        const auto place = [&](const QString &key, QPoint point)
        {
            if (!m_positions.contains(key))
            {
                m_positions[key] = fittedPosition(point);
                m_automaticPositions.insert(key);
            }
        };
        place("volume", {right, primary.y() + 10});
        place("trash", {right, primary.y() + primary.height() - 96});
        QVector<QPoint> cells;
        for (const auto &area : areas)
        {
            const int columns = std::max(1, (area.width() - 20) / 104);
            const int rows = std::max(1, (area.height() - 10 - itemHeight) / 82 + 1);
            for (int column = 0; column < columns; ++column)
                for (int row = 0; row < rows; ++row)
                    cells.append(fittedPosition({area.x() + area.width() - itemWidth - 20 - column * 104,
                                                 area.y() + 10 + row * 82}));
        }
        QVector<QRect> occupied;
        for (const auto &entry : m_entries)
            if (m_positions.contains(entry.key))
                occupied.append(QRect(position(entry), QSize(itemWidth, itemHeight)));
        int next = 0;
        for (const auto &entry : m_entries)
        {
            if (m_positions.contains(entry.key))
                continue;
            QPoint point = cells.at(next % cells.size());
            // Visit a candidate at most once. An overfull desktop may overlap
            // icons, but must not rescan every occupied slot for every file.
            for (; next < cells.size(); ++next)
            {
                point = cells.at(next);
                if (std::none_of(occupied.cbegin(), occupied.cend(), [&](const QRect &rectangle)
                                 { return rectangle.intersects(QRect(point, QSize(itemWidth, itemHeight))); }))
                    break;
            }
            place(entry.key, point);
            occupied.append(QRect(point, QSize(itemWidth, itemHeight)));
            ++next;
        }
        return;
    }
    const int right = std::max(0, m_viewport.width() - itemWidth - 20);
    if (!m_positions.contains("volume"))
    {
        m_positions["volume"] = {right, 50};
        m_automaticPositions.insert("volume");
    }
    if (!m_positions.contains("trash"))
    {
        m_positions["trash"] = {right, std::max(topMargin, m_viewport.height() - 96)};
        m_automaticPositions.insert("trash");
    }
    const int rows = std::max(1, (m_viewport.height() - 50 - itemHeight) / 82 + 1);
    const int cellCount = rows * std::max(1, (m_viewport.width() - 20) / 104);
    QVector<QRect> occupied;
    for (const auto &entry : m_entries)
        if (m_positions.contains(entry.key))
            occupied.push_back(QRect(position(entry), QSize(itemWidth, itemHeight)));
    int slot = 1;
    for (const auto &entry : m_entries)
    {
        if (m_positions.contains(entry.key))
            continue;
        QPoint candidate;
        // Avoid occupied rectangles, including manually placed icons.
        for (;; ++slot)
        {
            const int cell = slot % cellCount;
            candidate = {std::max(0, right - (cell / rows) * 104), 50 + (cell % rows) * 82};
            const QRect rectangle(candidate, QSize(itemWidth, itemHeight));
            if (slot >= cellCount ||
                std::none_of(occupied.begin(), occupied.end(),
                             [&](const QRect &other) { return other.intersects(rectangle); }))
                break;
        }
        m_positions[entry.key] = candidate;
        m_automaticPositions.insert(entry.key);
        occupied.push_back(QRect(candidate, QSize(itemWidth, itemHeight)));
        ++slot;
    }
}
void DesktopItems::select(QString key, bool extend)
{
    if (std::none_of(m_entries.begin(), m_entries.end(),
                     [&](const auto &entry) { return entry.key == key; }))
        return;
    if (extend)
    {
        if (m_selected.contains(key))
            m_selected.remove(key);
        else
            m_selected.insert(key);
    }
    else if (!m_selected.contains(key))
        m_selected = {key};
    notifySelection();
}
void DesktopItems::notifySelection()
{
    ++m_selectionEpoch;
    if (!m_entries.isEmpty())
        emit dataChanged(index(0), index(rowCount() - 1), {SelectedRole});
    emit selectionChanged();
}
void DesktopItems::clearSelection()
{
    if (m_selected.isEmpty())
        return;
    m_selected.clear();
    notifySelection();
}
void DesktopItems::selectAll()
{
    m_selected.clear();
    for (const auto &entry : m_entries)
        m_selected.insert(entry.key);
    notifySelection();
}
bool DesktopItems::beginSelection(int x, int y, bool extend)
{
    if (m_busy || dragging() || (m_displayLayout && !acceptsPoint(x, y)))
        return false;
    endSelection();
    m_selectionBefore = m_selected;
    m_selectionBase = extend ? m_selected : QSet<QString>{};
    m_selectionOrigin = {std::clamp(x, 0, m_viewport.width() - 1),
                         std::clamp(y, m_displayLayout ? 0 : 26, m_viewport.height() - 1)};
    m_selectionPointer = m_selectionOrigin;
    m_selecting = true;
    if (m_selected != m_selectionBase)
    {
        m_selected = m_selectionBase;
        notifySelection();
    }
    emit selectionAreaChanged();
    return true;
}
void DesktopItems::updateSelection(int x, int y)
{
    if (!m_selecting)
        return;
    m_selectionPointer = {std::clamp(x, 0, m_viewport.width() - 1),
                          std::clamp(y, m_displayLayout ? 0 : 26, m_viewport.height() - 1)};
    QSet<QString> selected;
    const auto rectangle = selectionRect();
    for (const auto &entry : m_entries)
        if (m_selectionBase.contains(entry.key) ||
            (m_selectionOrigin != m_selectionPointer &&
             rectangle.intersects(QRect(position(entry), QSize(itemWidth, itemHeight)))))
            selected.insert(entry.key);
    if (selected != m_selected)
    {
        m_selected = std::move(selected);
        notifySelection();
    }
    emit selectionAreaChanged();
}
void DesktopItems::endSelection()
{
    if (!m_selecting)
        return;
    m_selecting = false;
    m_selectionBefore.clear();
    m_selectionBase.clear();
    emit selectionAreaChanged();
}
void DesktopItems::cancelSelection()
{
    if (!m_selecting)
        return;
    QSet<QString> restored;
    for (const auto &entry : m_entries)
        if (m_selectionBefore.contains(entry.key))
            restored.insert(entry.key);
    if (m_selected != restored)
    {
        m_selected = std::move(restored);
        notifySelection();
    }
    endSelection();
}
void DesktopItems::moveSelected(int dx, int dy)
{
    if (m_selected.isEmpty() || m_busy)
        return;
    QRect bounds;
    for (const auto &entry : m_entries)
        if (m_selected.contains(entry.key))
            bounds |= QRect(position(entry), QSize(itemWidth, itemHeight));
    dx = std::clamp(dx, -bounds.left(), m_viewport.width() - 1 - bounds.right());
    dy = std::clamp(dy, (m_displayLayout ? 0 : topMargin) - bounds.top(), m_viewport.height() - 1 - bounds.bottom());
    for (const auto &entry : m_entries)
        if (m_selected.contains(entry.key))
        {
            m_positions[entry.key] = position(entry) + QPoint(dx, dy);
            m_automaticPositions.remove(entry.key);
        }
    if (!m_entries.isEmpty())
        emit dataChanged(index(0), index(rowCount() - 1), {XRole, YRole});
}
void DesktopItems::beginDrag()
{
    if (m_busy)
        return;
    m_dragPositions.clear();
    for (const auto &entry : m_entries)
        if (m_selected.contains(entry.key))
        {
            const auto shown = position(entry);
            m_dragPositions[entry.key] = m_positions.value(entry.key);
            if (m_displayLayout)
                m_positions[entry.key] = shown;
        }
    m_dropTarget.clear();
    emit dragChanged();
}
void DesktopItems::cancelDrag()
{
    if (m_dragPositions.isEmpty())
        return;
    for (const auto &entry : m_entries)
        if (m_dragPositions.contains(entry.key))
            m_positions[entry.key] = m_dragPositions.value(entry.key);
    m_dragPositions.clear();
    m_dropTarget.clear();
    emit dragChanged();
    if (!m_entries.isEmpty())
        emit dataChanged(index(0), index(rowCount() - 1), {XRole, YRole});
}
const DesktopItems::Entry *DesktopItems::dropEntry(int x, int y) const
{
    if (!canModifySelection() || (m_displayLayout && !acceptsPoint(x, y)))
        return nullptr;
    for (auto it = m_entries.crbegin(); it != m_entries.crend(); ++it)
        if (!m_selected.contains(it->key) &&
            QRect(position(*it), QSize(itemWidth, itemHeight)).contains(x, y) &&
            (it->kind == "trash" || m_operations.readableDirectory(it->path)))
            return &*it;
    return nullptr;
}
void DesktopItems::updateDropTarget(int x, int y)
{
    const auto *target = dragging() ? dropEntry(x, y) : nullptr;
    const auto key = target ? target->key : QString();
    if (m_dropTarget != key)
    {
        m_dropTarget = key;
        emit dragChanged();
    }
}
void DesktopItems::finishDrag(int x, int y, bool copy)
{
    if (m_dragPositions.isEmpty() || m_busy)
        return;
    const auto *target = dropEntry(x, y);
    const auto targetPath = target ? target->path : QString();
    const bool trash = target && target->kind == "trash";
    if (trash || !targetPath.isEmpty())
    {
        // File drops are distinct from placement. Restore source positions even
        // when a move/copy fails, or when copying leaves the originals behind.
        cancelDrag();
        startOperation(trash  ? Operation::trash
                       : copy ? Operation::copy
                              : Operation::move,
                       selectedFilePaths(), targetPath);
    }
    else
    {
        const auto placed = QSet<QString>(m_dragPositions.keyBegin(), m_dragPositions.keyEnd());
        m_dragPositions.clear();
        settlePlacement(placed);
        m_dropTarget.clear();
        emit dragChanged();
        if (m_displayLayout && !m_entries.isEmpty())
            emit dataChanged(index(0), index(rowCount() - 1), {XRole, YRole});
        savePositions();
    }
}
bool DesktopItems::savePositions()
{
    QJsonObject positions;
    QJsonObject identities;
    for (const auto &entry : m_entries)
    {
        const auto p = m_positions.value(entry.key);
        positions[entry.key] = QJsonArray{p.x(), p.y()};
        if (!entry.filesystemIdentity.isEmpty())
            identities[entry.name] =
                QJsonObject{{"key", entry.key}, {"filesystem", entry.filesystemIdentity}};
    }
    QSaveFile file(m_profile + "/desktop.json");
    const auto document = QJsonDocument(QJsonObject{{"version", 1},
                                                    {"workspace", m_root},
                                                    {"positions", positions},
                                                    {"entries", identities}})
                              .toJson();
    if (file.open(QIODevice::WriteOnly) && file.write(document) == document.size() && file.commit())
        return true;
    emit errorOccurred("Desktop positions could not be saved: " + file.errorString());
    return false;
}
void DesktopItems::loadPositions()
{
    QFile file(m_profile + "/desktop.json");
    if (!file.open(QIODevice::ReadOnly) || file.size() > 32 * 1024 * 1024)
        return;
    const auto object = QJsonDocument::fromJson(file.readAll()).object();
    if (object.value("version").toInt() != 1 || object.value("workspace").toString() != m_root)
        return;
    const auto positions = object.value("positions").toObject();
    if (positions.size() > maximumEntries + 2)
        return;
    for (auto it = positions.begin(); it != positions.end(); ++it)
    {
        const auto point = it.value().toArray();
        if (point.size() != 2 || !point[0].isDouble() || !point[1].isDouble())
            continue;
        const int x = point[0].toInt(-1), y = point[1].toInt(-1);
        if (x >= 0 && x <= 100000 && y >= topMargin && y <= 100000)
            m_positions[it.key()] = {x, y};
    }
    const auto identities = object.value("entries").toObject();
    if (identities.size() > maximumEntries)
        return;
    for (auto it = identities.begin(); it != identities.end(); ++it)
    {
        const auto record = it.value().toObject();
        const auto key = record.value("key").toString(),
                   filesystem = record.value("filesystem").toString();
        if (it.key().isEmpty() || it.key().contains('/') || key.isEmpty() || key == "volume" ||
            key == "trash" || key.size() > 1024 || filesystem.isEmpty() || filesystem.size() > 1024)
            continue;
        m_rememberedEntries.push_back({key, it.key(), {}, {}, filesystem});
    }
}
void DesktopItems::openSelected()
{
    if (m_busy)
        return;
    // Signals may launch clients, but never execute a desktop file as a program.
    for (const auto &entry : m_entries)
        if (m_selected.contains(entry.key))
            emit openRequested(entry.kind, entry.path);
}
void DesktopItems::cleanUp()
{
    if (m_busy)
        return;
    m_positions.clear();
    arrangeMissing();
    if (!m_entries.isEmpty())
        emit dataChanged(index(0), index(rowCount() - 1), {XRole, YRole});
    savePositions();
}
QStringList DesktopItems::selectedFilePaths() const
{
    QStringList paths;
    for (const auto &entry : m_entries)
        if (m_selected.contains(entry.key))
        {
            if (entry.filesystemIdentity.isEmpty())
                return {}; // Disk/Trash are not file jobs.
            paths.push_back(entry.path);
        }
    return paths;
}
bool DesktopItems::canModifySelection() const
{
    return !selectedFilePaths().isEmpty();
}
QString DesktopItems::selectedDocument() const
{
    const auto paths = selectedFilePaths();
    return paths.size() == 1 && m_operations.readableFile(paths.first()) ? paths.first()
                                                                         : QString();
}
QVariantMap DesktopItems::renameTarget() const
{
    if (m_selected.size() != 1)
        return {};
    for (const auto &entry : m_entries)
        if (m_selected.contains(entry.key) && !entry.filesystemIdentity.isEmpty())
            return {{"key", entry.key}, {"name", entry.kind == "launcher" ? QFileInfo(entry.path).fileName() : entry.name}};
    return {};
}
bool DesktopItems::createFolder(QString name)
{
    return startOperation(Operation::create, {m_directory}, std::move(name));
}
bool DesktopItems::renameItem(QString key, QString name)
{
    for (const auto &entry : m_entries)
        if (entry.key == key && !entry.filesystemIdentity.isEmpty())
            return startOperation(Operation::rename, {entry.path}, std::move(name), entry);
    emit errorOccurred("The selected desktop item is no longer available for renaming.");
    return false;
}
bool DesktopItems::duplicateSelected()
{
    return startOperation(Operation::duplicate, selectedFilePaths());
}
bool DesktopItems::trashSelected()
{
    return startOperation(Operation::trash, selectedFilePaths());
}
QVariantMap DesktopItems::trashSummary() const
{
    const auto items = m_operations.trashedItems();
    qint64 bytes = 0;
    int visited = 0;
    bool complete = true;
    for (const auto &item : items)
    {
        const QFileInfo top(item);
        if (!top.isDir() || top.isSymLink())
        {
            bytes += top.isSymLink() ? 0 : top.size();
            continue;
        }
        // Bounded walk for the confirmation text only; links are not followed.
        QDirIterator walk(item, QDir::Files | QDir::Hidden | QDir::System | QDir::NoSymLinks,
                          QDirIterator::Subdirectories);
        while (walk.hasNext())
        {
            if (++visited > 50000)
            {
                complete = false;
                break;
            }
            bytes += walk.nextFileInfo().size();
        }
    }
    return {{"items", int(items.size())}, {"bytes", bytes}, {"complete", complete}};
}
bool DesktopItems::emptyTrash()
{
    const auto items = m_operations.trashedItems();
    if (items.isEmpty())
    {
        emit errorOccurred("The Trash is empty.");
        return false;
    }
    return startOperation(Operation::erase, items);
}
QString DesktopItems::selectionInfo() const
{
    if (m_selected.size() != 1)
        return {};
    for (const auto &entry : m_entries)
        if (m_selected.contains(entry.key))
        {
            const QFileInfo file(entry.path);
            return QString("%1\n\nKind: %2\nLocation: %3\nSize: %4 bytes")
                .arg(entry.name, entry.kind, entry.path)
                .arg(file.size());
        }
    return {};
}
bool DesktopItems::typeSelect(const QString &text)
{
    if (text.isEmpty() || m_entries.isEmpty())
        return false;
    QCollator collator;
    collator.setCaseSensitivity(Qt::CaseInsensitive);
    const Entry *atOrAfter = nullptr, *last = nullptr;
    for (const auto &entry : m_entries)
    {
        if (!last || collator.compare(entry.name, last->name) > 0)
            last = &entry;
        if (collator.compare(entry.name, text) >= 0 &&
            (!atOrAfter || collator.compare(entry.name, atOrAfter->name) < 0))
            atOrAfter = &entry;
    }
    const auto *chosen = atOrAfter ? atOrAfter : last;
    m_selected = {chosen->key};
    notifySelection();
    return true;
}
QString DesktopItems::selectedNames() const
{
    QVector<const Entry *> selected;
    for (const auto &entry : m_entries)
        if (m_selected.contains(entry.key))
            selected.push_back(&entry);
    std::stable_sort(selected.begin(), selected.end(), [this](const Entry *a, const Entry *b) {
        const auto first = position(*a), second = position(*b);
        return first.y() != second.y() ? first.y() < second.y() : first.x() < second.x();
    });
    QStringList names;
    for (const auto *entry : selected)
        names.push_back(entry->name);
    return names.join('\n');
}
QVariantMap DesktopItems::selectedDetails() const
{
    if (m_selected.size() != 1)
        return {};
    const auto entry = std::find_if(m_entries.cbegin(), m_entries.cend(),
                                    [&](const Entry &e) { return m_selected.contains(e.key); });
    if (entry == m_entries.cend())
        return {};
    auto result = fileDetails(entry->path, entry->kind);
    result.insert({{"key", entry->key}, {"path", entry->path}, {"name", entry->name}, {"kind", entry->kind}});
    return result;
}
const DesktopItems::Entry *DesktopItems::externalDropEntry(QPoint point) const
{
    if (m_displayLayout && !acceptsPoint(point.x(), point.y()))
        return nullptr;
    for (auto it = m_entries.crbegin(); it != m_entries.crend(); ++it)
        if (QRect(position(*it), QSize(itemWidth, itemHeight)).contains(point) &&
            (it->kind == "trash" || m_operations.readableDirectory(it->path)))
            return &*it;
    return nullptr;
}
void DesktopItems::externalDragAt(int x, int y)
{
    const auto *target = externalDropEntry({x, y});
    const auto key = target ? target->key : QString();
    if (m_dropTarget != key)
    {
        m_dropTarget = key;
        emit dragChanged();
    }
}
void DesktopItems::clearExternalDrag()
{
    if (m_dropTarget.isEmpty())
        return;
    m_dropTarget.clear();
    emit dragChanged();
}
bool DesktopItems::beginWindowDrop(bool copy)
{
    if (m_busy || !dragging())
        return false;
    QStringList paths;
    for (const auto &entry : m_entries)
        if (m_dragPositions.contains(entry.key))
        {
            if (entry.filesystemIdentity.isEmpty())
            {
                cancelDrag();
                return false;
            }
            paths.push_back(entry.path);
        }
    cancelDrag();
    if (paths.isEmpty() || paths.size() > 128)
        return false;
    m_windowDropPaths = std::move(paths);
    m_windowDropCopy = copy;
    m_busy = true;
    m_operationStatus = "Preparing transfer…";
    emit operationChanged();
    return true;
}
bool DesktopItems::completeWindowDrop(QString directory, QString error)
{
    if (m_windowDropPaths.isEmpty())
        return false;
    auto paths = std::exchange(m_windowDropPaths, {});
    const bool copy = m_windowDropCopy;
    m_busy = false;
    m_operationStatus.clear();
    if (!error.isEmpty() || directory.isEmpty() || (!copy && directory == m_directory))
    {
        emit operationChanged();
        if (!error.isEmpty())
            emit errorOccurred(error);
        return error.isEmpty();
    }
    return startOperation(directory == m_operations.trashBase() + "/files" ? Operation::trash
                          : copy                                           ? Operation::copy
                                                                           : Operation::move,
                          std::move(paths), std::move(directory));
}
bool DesktopItems::receiveFiles(QStringList paths, QPoint point, bool copy)
{
    if (m_displayLayout && !acceptsPoint(point.x(), point.y()))
    {
        emit errorOccurred("Drop the files on a connected display.");
        return false;
    }
    if (paths.isEmpty() || paths.size() > 128)
        return false;
    paths.removeDuplicates();
    // Validate the whole batch before starting. Backend checks are repeated in
    // the worker; this is an ordinary user process, not a race-proof sandbox.
    for (const auto &path : paths)
    {
        const auto parent = QFileInfo(path).dir().canonicalPath();
        if (!QDir::isAbsolutePath(path) || path.contains(QChar::Null) ||
            (parent != m_root && !parent.startsWith(m_root + '/')) || path == m_root ||
            (!QFileInfo::exists(path) && !QFileInfo(path).isSymLink()))
        {
            emit errorOccurred(
                "The drop contains an unavailable item or an item outside the selected workspace.");
            return false;
        }
    }
    const auto *target = externalDropEntry(point);
    const bool trash = target && target->kind == "trash";
    const auto directory = target && !trash ? target->path : m_directory;
    if (!copy && !trash)
        paths.removeIf([&](const QString &path)
                       { return QFileInfo(path).absolutePath() == directory; });
    clearExternalDrag();
    if (paths.isEmpty())
        return true; // Already in the requested directory.
    return startOperation(trash  ? Operation::trash
                          : copy ? Operation::copy
                                 : Operation::move,
                          std::move(paths), directory, {}, target ? QPoint(-1, -1) : point);
}
bool DesktopItems::startOperation(Operation operation, QStringList paths, QString name,
                                  Entry renamed, QPoint landing)
{
    if (m_busy)
    {
        emit errorOccurred("Wait for the current desktop file operation to finish.");
        return false;
    }
    if (paths.isEmpty())
    {
        emit errorOccurred("Select files or folders for this operation. The disk and Trash are not "
                           "file selections.");
        return false;
    }
    cancelDrag();
    m_busy = true;
    switch (operation)
    {
    case Operation::create:
        m_operationStatus = "Creating folder…";
        break;
    case Operation::rename:
        m_operationStatus = "Renaming…";
        break;
    case Operation::duplicate:
        m_operationStatus = "Duplicating…";
        break;
    case Operation::trash:
        m_operationStatus = "Moving to Trash…";
        break;
    case Operation::move:
        m_operationStatus = "Moving…";
        break;
    case Operation::copy:
        m_operationStatus = "Copying…";
        break;
    case Operation::erase:
        m_operationStatus = "Emptying Trash…";
        break;
    }
    emit operationChanged();
    const auto epoch = m_selectionEpoch;
    const auto oldPosition = m_positions.value(renamed.key);
    const int requested = paths.size();
    auto *watcher = new QFutureWatcher<OperationResult>(this);
    connect(watcher, &QFutureWatcher<OperationResult>::finished, this,
            [this, watcher, operation, renamed, oldPosition, epoch, requested, landing]
            {
                const auto result = watcher->result();
                watcher->deleteLater();
                m_busy = false;
                if (operation == Operation::rename && !result.completed.isEmpty())
                {
                    auto entry = renamed;
                    entry.path = result.completed.first();
                    const QFileInfo file(entry.path);
                    entry.name = file.fileName();
                    entry.filesystemIdentity = entryKey(file);
                    m_rememberedEntries.removeIf([&](const Entry &other)
                                                 { return other.key == entry.key; });
                    m_rememberedEntries.push_back(entry);
                    m_positions[entry.key] = oldPosition;
                }
                refresh();
                if (landing.x() >= 0)
                {
                    auto point = landing;
                    QSet<QString> landed;
                    for (const auto &entry : m_entries)
                        if (result.completed.contains(entry.path))
                        {
                            m_positions[entry.key] = {
                                std::clamp(point.x() - 50, 0,
                                           std::max(0, m_viewport.width() - itemWidth)),
                                std::clamp(point.y() - 20, topMargin,
                                           std::max(topMargin, m_viewport.height() - itemHeight))};
                            landed.insert(entry.key);
                            point += QPoint(20, 20);
                        }
                    settlePlacement(landed);
                    if (!m_entries.isEmpty())
                        emit dataChanged(index(0), index(rowCount() - 1), {XRole, YRole});
                }
                if (epoch == m_selectionEpoch && !result.completed.isEmpty())
                {
                    if (operation == Operation::create || operation == Operation::rename ||
                        operation == Operation::duplicate)
                    {
                        m_selected.clear();
                        for (const auto &entry : m_entries)
                            if (result.completed.contains(entry.path))
                                m_selected.insert(entry.key);
                        notifySelection();
                    }
                }
                savePositions();
                const bool success = result.error.isEmpty();
                m_operationStatus =
                    success ? QString("Completed: %1 item(s)").arg(result.completed.size())
                            : QString("%1 of %2 item(s) completed")
                                  .arg(result.completed.size())
                                  .arg(requested);
                emit operationChanged();
                emit operationFinished(success, result.completed);
                if (!success)
                    emit errorOccurred(m_operationStatus + "\n\n" + result.error);
            });
    const auto operations = m_operations;
    watcher->setFuture(QtConcurrent::run(
        [operations, operation, paths, name]
        {
            OperationResult result;
            for (const auto &path : paths)
            {
                FileResult item;
                switch (operation)
                {
                case Operation::create:
                    item = operations.createFolder(path, name);
                    break;
                case Operation::rename:
                    item = operations.rename(path, name);
                    break;
                case Operation::duplicate:
                    item = operations.duplicate(path);
                    break;
                case Operation::trash:
                    item = operations.trash(path);
                    break;
                case Operation::move:
                    item = operations.move(path, name);
                    break;
                case Operation::copy:
                    item = operations.copy(path, name);
                    break;
                case Operation::erase:
                    item = operations.removeFromTrash(path);
                    break;
                }
                if (!item.ok)
                {
                    result.error = item.error;
                    break;
                }
                result.completed.push_back(item.path);
            }
            return result;
        }));
    return true;
}

bool DesktopItems::trustedLauncher(const QFileInfo &file)
{
    return file.suffix() == "desktop" && file.isFile() && !file.isSymLink() && file.ownerId() == getuid() &&
           file.permission(QFileDevice::ExeOwner) && file.size() <= 64 * 1024;
}
